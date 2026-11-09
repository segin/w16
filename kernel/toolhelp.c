/*
 * toolhelp.c - what TOOLHELP.DLL and debuggers get from KERNEL: the
 * THHOOK structure (KERNEL.332), the notification hooks ToolHelpHook
 * (KERNEL.341) and RegisterPtrace (KERNEL.202) and the notifications
 * KERNEL sends through them, FatalExitHook (318), InitTask1 (141),
 * GetSetKernelDOSProc (311), DiagQuery (339), GetLPErrMode (99).
 *
 * Sources: Schulman et al., "Undocumented Windows" (1992), ch. 5 (THHook,
 * ToolHelpHook, RegisterPtrace) and Pietrek, "Windows Internals" (1993)
 * for the names and the KRNL386 layout; facts only. Everything below is
 * as observed on Windows 3.1 under KRNL286 (tests/apps/tk50.c, tk51.c,
 * run as the shell with Microsoft's KRNL286 and with w16's: tests/dropin.sh)
 * unless marked UNVERIFIED.
 *
 * THHOOK, observed under KRNL286 (TK51, ARCHITECTURE.md 9.7):
 *   00h hGlobalHeap, 02h pGlobalHeap (GlobalMasterHandle: both the
 *   Burgermaster's selector), 04h hExeHead (KERNEL's module table), 06h
 *   0, 08h TopPDB (KERNEL's PSP), 0Ah HeadPDB (with one task: its PDB),
 *   0Ch 0, 0Eh HeadTDB, 10h CurTDB, 12h LoadTDB (0), 14h LockTDB (0),
 *   16h SelTableLen (3FFCh), 18h SelTableStart (dword, 72h), 24h hGDI,
 *   26h hUSER, 28h the shell's module. KRNL286 puts hGDI, hUSER and the
 *   shell 4 bytes before where Schulman has them for KRNL386.
 *   UNVERIFIED: the other words (1Ch, 20h, 2Eh-3Ch, ...: values with no
 *   known meaning, left 0 here), HeadPDB with several tasks (w16: the
 *   PDB of the first task in the list).
 *
 * Notifications (KERNEL calls the ToolHelpHook procedure, else the
 * RegisterPtrace one, with AX = the code, DS = KERNEL's data segment; TK51
 * recorded every register), and what TOOLHELP makes of them (NFY_*):
 *   12h  OutputDebugString: ES:SI = the string                 (OUTSTR)
 *   50h  a segment loaded: BX = its number - 1, CX = selector, DX = 0,
 *        SI = its flags in the module table, ES:DI = module name (LOADSEG)
 *   52h  a segment freed: BX = selector                        (FREESEG)
 *   59h  a task starts, on its stack before its entry point: CX:BX =
 *        the entry point (after the task-in notification)      (STARTTASK)
 *   62h  a task ends: BL = the exit code                       (EXITTASK)
 *   64h  a DLL is initialised: CX:BX = entry point (0:0 for none), SI =
 *        hModule                                               (STARTDLL)
 *   65h  a module is deleted: ES = hModule                     (DELMODULE)
 *   0Dh  a task is switched out, 0Eh one switched in    (TASKOUT/TASKIN)
 *   66h  LogError: CX = code, DX:BX = data; 67h LogParamError: ES:BX ->
 *        {code, address, parameter} (Schulman; UNVERIFIED)
 * Windows 3.1 loads segments on demand and notifies each as it comes in;
 * w16 loads every segment with its module and notifies them all then.
 */
#include "kernel.h"

void FAR * KAPI GlobalLock(u16 h);
u32  KAPI GlobalMasterHandle(void);
u16  KAPI IsTaskLocked(void);
extern u16 __cdecl ptrace_call(u32 proc, u16 ax, u16 bx, u16 cx, u16 dx,
                               u16 si, u16 di, u16 es);
int  diag_query(void);                  /* winboot.c */

/* THHOOK: KERNEL.332, a data entry */
u16 thhook_w[0x50];
#pragma aux thhook_w "THHOOK"

#define TH_HGLOBALHEAP  0x00
#define TH_PGLOBALHEAP  0x01
#define TH_HEXEHEAD     0x02
#define TH_TOPPDB       0x04
#define TH_HEADPDB      0x05
#define TH_HEADTDB      0x07
#define TH_CURTDB       0x08
#define TH_LOADTDB      0x09
#define TH_LOCKTDB      0x0A
#define TH_SELTABLEN    0x0B
#define TH_SELTABSTART  0x0C            /* dword */
#define TH_HGDI         0x12
#define TH_HUSER        0x13
#define TH_HSHELL       0x14

static u32 hook_th;                     /* ToolHelpHook */
static u32 hook_pt;                     /* RegisterPtrace */
static u32 fatal_hook;                  /* FatalExitHook */
static u8  err_mode;                    /* GetLPErrMode's byte */

/* Bring THHOOK up to date (task switches, the task list, the boot). */
void thhook_sync(void)
{
    u32 m = GlobalMasterHandle();
    u16 head = (u16)(GetCurrentTask() >> 16);
    module FAR *x;
    thhook_w[TH_HGLOBALHEAP] = (u16)m;
    thhook_w[TH_PGLOBALHEAP] = (u16)(m >> 16);
    thhook_w[TH_HEXEHEAD] = module_first ? module_first->ne_sel : 0;
    thhook_w[TH_TOPPDB] = psp_sel;
    thhook_w[TH_HEADTDB] = head;
    thhook_w[TH_CURTDB] = cur_task;
    /* UNVERIFIED with several tasks */
    thhook_w[TH_HEADPDB] = head ? *(u16 FAR *)MK_FP(head, 0x60) : 0;
    thhook_w[TH_LOCKTDB] = IsTaskLocked();
    thhook_w[TH_SELTABLEN] = 0x3FFC;    /* observed */
    *(u32 *)&thhook_w[TH_SELTABSTART] = 0x72;
    if (!thhook_w[TH_HGDI] && (x = module_find("GDI")) != NULL)
        thhook_w[TH_HGDI] = x->ne_sel;
    if (!thhook_w[TH_HUSER] && (x = module_find("USER")) != NULL)
        thhook_w[TH_HUSER] = x->ne_sel;
}

void thhook_set_shell(u16 hmod)
{
    thhook_w[TH_HSHELL] = hmod;
}

/* ---- notifications ---- */

/* KERNEL keeps two procedures; the ToolHelpHook one gets the
   notifications, and the RegisterPtrace one only when there is no
   ToolHelpHook one, or for a task start (Schulman). */
static void notify(u16 ax, u16 bx, u16 cx, u16 dx, u16 si, u16 di, u16 es)
{
    if (hook_th)
        ptrace_call(hook_th, ax, bx, cx, dx, si, di, es);
    if (hook_pt && (!hook_th || ax == 0x59))
        ptrace_call(hook_pt, ax, bx, cx, dx, si, di, es);
}

int th_hooked(void)
{
    return hook_th || hook_pt;
}

void th_outstr(const char FAR *s)
{
    if (th_hooked())
        notify(0x12, 0, 0, 0, FP_OFF(s), 0, FP_SEG(s));
}

/* the segments of a module just loaded */
void th_loadsegs(module FAR *m)
{
    u16 i;
    if (!th_hooked())
        return;
    for (i = 0; i < m->seg_count; i++)
        notify(0x50, i, m->seg[i].sel, 0, MOD_SEG(m, i)->flags,
               FP_OFF(m->name), FP_SEG(m->name));
}

/* a DLL's initialisation is next: cs:ip its entry point (0:0 for none) */
void th_startdll(module FAR *m, u16 cs, u16 ip)
{
    if (th_hooked())
        notify(0x64, ip, cs, 0, m->ne_sel, 0, 0);
}

/* a module is deleted: the notification, then one per freed segment */
void th_delmodule(module FAR *m)
{
    u16 i;
    if (!th_hooked())
        return;
    notify(0x65, 0, 0, 0, 0, 0, m->ne_sel);
    for (i = 0; i < m->seg_count; i++)
        if (m->seg[i].hnd)
            notify(0x52, m->seg[i].sel, 0, 0, 0, 0, 0);
    if (m->ne_sel)
        notify(0x52, m->ne_sel, 0, 0, 0, 0, 0);
}

void th_task_out(void)
{
    if (th_hooked())
        notify(0x0D, 0, 0, 0, 0, 0, 0);
}

void th_task_in(void)
{
    thhook_sync();
    if (th_hooked())
        notify(0x0E, 0, 0, 0, 0, 0, 0);
}

/* a new task's first switch (khook.asm, from task_tramp): it is in, and
   starts at cs:ip */
void __cdecl __far th_task_start(u16 ip, u16 cs)
{
    th_task_in();
    if (th_hooked())
        notify(0x59, ip, cs, 0, 0, 0, 0);
}

void th_task_exit(u16 code)
{
    if (th_hooked())
        notify(0x62, code & 0xFF, 0, 0, 0, 0, 0);
}

void th_logerror(u16 err, void FAR *info)
{
    if (th_hooked())
        notify(0x66, FP_OFF(info), err, FP_SEG(info), 0, 0, 0);
}

void th_logparamerror(u16 err, void FAR *proc, void FAR *param)
{
    static u16 rec[5];          /* UNVERIFIED: Schulman's layout */
    if (!th_hooked())
        return;
    rec[0] = err;
    *(void FAR **)&rec[1] = proc;
    *(void FAR **)&rec[3] = param;
    notify(0x67, FP_OFF((void FAR *)rec), 0, 0, 0, 0, FP_SEG((void FAR *)rec));
}

/* ToolHelpHook (KERNEL.341): set the notification procedure (0: none);
   returns the previous one (observed: 0 at first). */
u32 KAPI ToolHelpHook(u32 proc)
{
    u32 old = hook_th;
    hook_th = proc;
    return old;
}

/* RegisterPtrace (KERNEL.202): Schulman: the same for debuggers, with no
   result. UNVERIFIED: what it leaves in DX:AX. */
void KAPI RegisterPtrace(u32 proc)
{
    hook_pt = proc;
}

/* FatalExitHook (KERNEL.318): returns the previous procedure. */
u32 KAPI_ES FatalExitHook(u32 proc)
{
    u32 old = fatal_hook;
    fatal_hook = proc;
    return old;
}

u32 fatal_exit_hook(void)
{
    return fatal_hook;
}

/* InitTask1 (KERNEL.141): Schulman: sets the procedure a task's start-up
   would call ("Dressed for Success"). Observed under KRNL286: not in
   THHOOK 44h (that is KRNL386's place), and AX comes back changed.
   UNVERIFIED: where KRNL286 keeps it, and whether anything calls it
   (w16 keeps it and calls nothing). */
static u32 dressed;

void KAPI InitTask1(u32 proc)
{
    dressed = proc;
}

/* GetSetKernelDOSProc (KERNEL.311): the INT 21h procedure KERNEL hands
   DOS calls on to; returns the previous one. Observed on 3.1 (TK50): at
   first the DOS extender's (0053:02D2 under DOSX); KERNEL's own INT 21h
   handler, DOS3Call and NoHookDOSCall all go through the new one. In
   w16 that is old21, where kentry.asm's INT 21h hook chains to. */
static u16 cli_save(void);
#pragma aux cli_save = "pushf" "pop ax" "cli" value [ax] modify exact [ax];
static void flags_restore(u16 f);
#pragma aux flags_restore = "push ax" "popf" parm [ax] modify exact [];

u32 KAPI_ES GetSetKernelDOSProc(u32 proc)
{
    u16 f = cli_save();             /* no INT 21h through half a pointer */
    u32 old = old21;
    old21 = proc;
    flags_restore(f);
    return old;
}

/* DiagQuery (KERNEL.339): whether Windows writes BOOTLOG.TXT (WIN /B). */
u16 KAPI DiagQuery(void)
{
    return diag_query() != 0;
}

/* GetLPErrMode (KERNEL.99): a pointer to a byte in KERNEL's data
   segment, 0 (observed; Schulman: 1 while a critical-error box is up). */
u8 FAR * KAPI GetLPErrMode(void)
{
    return (u8 FAR *)&err_mode;
}
