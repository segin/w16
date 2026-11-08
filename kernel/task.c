/*
 * task.c - tasks: the task database (TDB), starting and ending tasks,
 * the non-preemptive scheduler (Yield, WaitEvent, PostEvent, ...),
 * InitTask, and the task APIs.
 *
 * Layout and behaviour observed black-box on Windows 3.1 with
 * tests/apps/tk15.c (Standard and 386 Enhanced mode agree; ARCHITECTURE.md
 * 9.2 lists the findings). The layout is the one published in Pietrek,
 * "Windows Internals" (1993), Table 3-2, and his chapter 6 describes the
 * scheduler (facts only; each one used here was checked by the probe
 * unless marked UNVERIFIED).
 *
 * TDB (a 200h-byte block in DOS memory; hTask is its selector, which is
 * also its global handle):
 *   00h next task      02h SS:SP when switched out   06h events
 *   08h priority       0Ch the TDB itself            14h FPU control word
 *   1Ah expected Windows version                     1Ch hInstance
 *   1Eh hModule        20h message queue             22h parent task
 *   60h PDB selector   62h DTA (far)                 66h drive + 80h
 *   67h directory ("\DIR", 41h bytes)                AAh DirectedYield task
 *   ACh implicit DLLs still to initialise (w16: a block of hModules)
 *   B0h code alias of the TDB   B2h - F1h MakeProcInstance thunks
 *   (procinst.c)       F2h module name (8 bytes)     FAh 'TD'
 *   100h the PDB (DOS PSP), which has a selector of its own
 *
 * Scheduling (observed traces agree with Pietrek's description): the task
 * list is ordered by priority; a task that is switched to moves behind the
 * other tasks of its priority, and a new task goes in front of them. The
 * scheduler runs the first task in the list with an event (DirectedYield's
 * task first, if it has one). Yield adds an event to the caller while it
 * looks (OldYield), WaitEvent takes one away and waits while there is
 * none. A task's directory, PSP and FPU control word are switched with it.
 */
#include "kernel.h"

/* InitTask's stack limit lies this far above the lowest address of the
   stack (observed on Windows 3.1: SP - limit = stack size - 96h). */
#define STACK_SLOP      0x0096

#define TDB_SIZE        0x200
#define TDB_NEXT        0x00
#define TDB_SSSP        0x02
#define TDB_EVENTS      0x06
#define TDB_PRIO        0x08
#define TDB_SELF        0x0C
#define TDB_FPCW        0x14
#define TDB_VERSION     0x1A
#define TDB_HINST       0x1C
#define TDB_HMOD        0x1E
#define TDB_QUEUE       0x20
#define TDB_PARENT      0x22
#define TDB_PDB         0x60
#define TDB_DTA         0x62
#define TDB_DRIVE       0x66
#define TDB_DIR         0x67
#define TDB_DIRLEN      0x41
#define TDB_YIELDTO     0xAA
#define TDB_LIBINIT     0xAC
#define TDB_ALIAS       0xB0
#define TDB_NAME        0xF2
#define TDB_SIG         0xFA
#define TDB_PSP         0x100
#define TDB_MAGIC       0x4454          /* "TD" */

#define TW(t, o)        (*(u16 FAR *)MK_FP((t), (o)))
#define TB(t, o)        (*(u8 FAR *)MK_FP((t), (o)))

u16 psp_sel;                    /* KERNEL's own PSP (the top PDB) */
u16 cur_task;                   /* the running task, 0 while booting */
u16 cur_pdb;                    /* its PDB */
static u16 task_head;
static u16 num_tasks;
static u16 in_sched;
static u16 boot_sssp[2];        /* KERNEL's stack while tasks run */
static u16 last_exit;           /* exit code of the last task */

/* What KERNEL keeps about a task besides its TDB (a block of its own). */
typedef struct taskx {
    struct taskx FAR *next;
    u16 self;                   /* selector of this block */
    u16 htask;
    module FAR *m;
    u16 dg_sel, dg_hnd;         /* its DGROUP */
    u16 stack_size, heap_size, sp;
    u16 hprev;
} taskx;

static taskx FAR *tx_first;

static taskx FAR *tx_of(u16 t)
{
    taskx FAR *x;
    for (x = tx_first; x; x = x->next)
        if (x->htask == t)
            return x;
    return NULL;
}

u16 get_winflags(void)
{
    u16 f = WF_PMODE | WF_STANDARD;
    u16 cpu = dpmi_cpu_type();
    if (cpu >= 4)
        f |= WF_CPU486;
    else if (cpu == 3)
        f |= WF_CPU386;
    else
        f |= WF_CPU286;
    /* BIOS equipment word (0040:0010), bit 1: math coprocessor present */
    if (*(u16 FAR *)MK_FP(dpmi_seg_to_desc(0x0040), 0x0010) & 2)
        f |= WF_80x87;
    return f;
}

module FAR *task_module(void)
{
    taskx FAR *x = cur_task ? tx_of(cur_task) : NULL;
    return x ? x->m : NULL;
}

u16 task_pdb(void)
{
    return cur_task ? cur_pdb : 0;
}

/* The module whose instance (DGROUP) sel is, through the tasks: for
   instances after the first, which the module's own table does not
   list. */
module FAR *task_module_of_sel(u16 sel)
{
    taskx FAR *x;
    for (x = tx_first; x; x = x->next)
        if ((x->dg_sel | 3) == (sel | 3))
            return x->m;
    return NULL;
}

/* Another instance's DGROUP of m than `not`, or 0. */
u16 task_other_dgroup(module FAR *m, u16 not_hnd, u16 *sel)
{
    taskx FAR *x;
    for (x = tx_first; x; x = x->next)
        if (x->m == m && x->dg_hnd != not_hnd) {
            *sel = x->dg_sel;
            return x->dg_hnd;
        }
    return 0;
}

/* The instance a new instance of m gets as hPrevInstance. UNVERIFIED with
   more than two instances (observed: the second gets the first). */
u16 task_prev_instance(module FAR *m)
{
    taskx FAR *x;
    for (x = tx_first; x; x = x->next)
        if (x->m == m)
            return x->dg_hnd;
    return 0;
}

static int is_task(u16 t)
{
    u16 x;
    for (x = task_head; x; x = TW(x, TDB_NEXT))
        if ((x | 3) == (t | 3))
            return 1;
    return 0;
}

static u16 boot_tdb;            /* the boot task (task_boot_tdb) */

static u16 task_of(u16 t)
{
    u16 x;
    if (!t)
        return 0;
    if (boot_tdb && (t | 3) == (boot_tdb | 3))
        return boot_tdb;
    for (x = task_head; x; x = TW(x, TDB_NEXT))
        if ((x | 3) == (t | 3))
            return x;
    return 0;
}

/* ---- DOS: real-mode calls through DPMI 0300h ---- */

static struct {
    u32 edi, esi, ebp, res, ebx, edx, ecx, eax;
    u16 flags, es, ds, fs, gs, ip, cs, sp, ss;
} rmc;

static u16 rm_int(u8 n, u16 ax, u16 bx, u16 dx, u16 si)
{
    regs16 r;
    kmemset(&rmc, 0, sizeof rmc);
    rmc.eax = ax;
    rmc.ebx = bx;
    rmc.edx = dx;
    rmc.esi = si;
    r.ax = 0x0300;
    r.bx = n;
    r.cx = 0;
    r.di = FP_OFF((void FAR *)&rmc);
    r.es = FP_SEG((void FAR *)&rmc);
    r.ds = 0;
    dpmi31(&r);
    return (u16)rmc.eax;
}

static u16 seg_of(u16 sel)
{
    return (u16)(dpmi_get_base(sel) >> 4);
}

/* Make pdb DOS's current PSP (the PDBs are in DOS memory). Windows
   switches it lazily (Pietrek); w16 does it with every task switch. */
static void set_psp(u16 pdb)
{
    rm_int(0x21, 0x5000, seg_of(pdb), 0, 0);
}

static u16 dos(u16 ax, u16 dx, u16 ds)
{
    regs16 r;
    r.ax = ax;
    r.dx = dx;
    r.ds = ds;
    r.es = 0;
    r.bx = r.cx = r.si = r.di = 0;
    if (dos21(&r))
        return 0xFFFF;
    return r.ax;
}

/* The current drive and directory into the TDB (66h, 67h), as observed:
   80h + drive (82h for C:) and "\WINDOWS". */
static void save_dir(u16 t)
{
    regs16 r;
    r.ax = 0x1900;                      /* (no carry to look at) */
    r.ds = r.es = 0;
    dos21(&r);
    TB(t, TDB_DRIVE) = (u8)(0x80 | (r.ax & 0xFF));
    TB(t, TDB_DIR) = '\\';
    r.ax = 0x4700;
    r.dx = 0;
    r.si = TDB_DIR + 1;
    r.ds = t;
    r.es = 0;
    r.bx = r.cx = r.di = 0;
    if (dos21(&r))
        TB(t, TDB_DIR + 1) = 0;
}

static void restore_dir(u16 t, u16 from)
{
    u16 i;
    if (from && TB(from, TDB_DRIVE) == TB(t, TDB_DRIVE)) {
        for (i = 0; i < TDB_DIRLEN && TB(from, TDB_DIR + i) == TB(t, TDB_DIR + i); i++)
            if (!TB(t, TDB_DIR + i))
                return;                 /* the same: nothing to do */
    }
    if (!TB(t, TDB_DRIVE))
        return;
    dos(0x0E00, TB(t, TDB_DRIVE) & 0x7F, 0);
    dos(0x3B00, TDB_DIR, t);
}

/* ---- FPU control word ---- */

static void fpu_save(u16 FAR *w);
#pragma aux fpu_save = ".8087" "fnstcw es:[bx]" "fwait" parm [es bx];
static void fpu_load(u16 FAR *w);
#pragma aux fpu_load = ".8087" "fnclex" "fldcw es:[bx]" parm [es bx];

/* ---- the task list ---- */

static void list_unlink(u16 t)
{
    u16 x;
    if (task_head == t) {
        task_head = TW(t, TDB_NEXT);
        return;
    }
    for (x = task_head; x; x = TW(x, TDB_NEXT))
        if (TW(x, TDB_NEXT) == t) {
            TW(x, TDB_NEXT) = TW(t, TDB_NEXT);
            return;
        }
}

/* In front of the first task whose priority is not below t's. */
static void list_insert(u16 t)
{
    signed char p = (signed char)TB(t, TDB_PRIO);
    u16 x, prev = 0;
    for (x = task_head; x && (signed char)TB(x, TDB_PRIO) < p; x = TW(x, TDB_NEXT))
        prev = x;
    TW(t, TDB_NEXT) = x;
    if (prev)
        TW(prev, TDB_NEXT) = t;
    else
        task_head = t;
}

/* Behind the other tasks of its priority. */
static void list_requeue(u16 t)
{
    TB(t, TDB_PRIO)++;
    list_unlink(t);
    list_insert(t);
    TB(t, TDB_PRIO)--;
}

/* ---- the scheduler ---- */

static void idle(void)
{
    rm_int(0x28, 0, 0, 0, 0);           /* DOS idle */
    rm_int(0x2F, 0x1689, 1, 0, 0);      /* Windows kernel idle */
}

static void switch_to(u16 t)
{
    u16 from = cur_task;
    static u16 have87 = 0xFFFF;

    if (have87 == 0xFFFF)
        have87 = (get_winflags() & WF_80x87) != 0;
    list_requeue(t);
    if (from) {
        save_dir(from);
        if (have87)
            fpu_save((u16 FAR *)MK_FP(from, TDB_FPCW));
    }
    cur_task = t;
    cur_pdb = TW(t, TDB_PDB);
    set_psp(cur_pdb);
    restore_dir(t, from);
    if (have87)
        fpu_load((u16 FAR *)MK_FP(t, TDB_FPCW));
    in_sched = 0;
    task_switch(from ? (u16 FAR *)MK_FP(from, TDB_SSSP) : NULL,
                TW(t, TDB_SSSP + 2), TW(t, TDB_SSSP));
}

/* Run the next task with an event; returns when the caller is run again
   (at once if that is the caller). */
static void reschedule(void)
{
    u16 t;
    if (in_sched || (boot_tdb && cur_task == boot_tdb))
        return;                         /* nothing to switch to yet */
    in_sched = 1;
    if (cur_task) {
        t = TW(cur_task, TDB_YIELDTO);
        TW(cur_task, TDB_YIELDTO) = 0;
        if (t && is_task(t) && TW(t, TDB_EVENTS)) {
            if (t != cur_task)
                switch_to(t);           /* clears in_sched */
            in_sched = 0;
            return;
        }
    }
    for (;;) {
        for (t = task_head; t && !TW(t, TDB_EVENTS); t = TW(t, TDB_NEXT))
            ;
        if (t)
            break;
        idle();
    }
    if (t != cur_task)
        switch_to(t);
    in_sched = 0;
}

/* ---- starting a task ---- */

/* Make the task for an instance of m whose DGROUP is dg (handle and
   selector), and link it into the task list with one event, so the
   scheduler starts it. tail is the command tail as LoadModule takes it
   (a length byte, the text, CR); show is LoadModule's {2, nCmdShow}. Returns the hTask, 0
   if out of memory. */
u16 task_create(module FAR *m, u16 dg_sel, u16 dg_hnd, u16 hprev,
                const u8 FAR *tail, const u16 FAR *show, u16 libinit)
{
    ne_header FAR *h = NE_HDR(m);
    u16 t, seg, pdb, alias, i, xsel, sp, n;
    u32 dsize;
    taskx FAR *x;
    u8 FAR *p;
    const ne_segment FAR *ds;

    if (!h->auto_data || h->auto_data > m->seg_count ||
        !h->cs || h->cs > m->seg_count || !h->ss || h->ss > m->seg_count)
        return 0;
    t = galloc_dos(TDB_SIZE, 0, &seg);
    if (!t)
        return 0;
    if (!galloc(GMEM_FIXED, sizeof(taskx), 0, &xsel)) {
        GlobalFree(t);
        return 0;
    }
    x = MK_FP(xsel, 0);
    pdb = dpmi_alloc_desc(1);
    if (!pdb || !dpmi_set_desc(pdb, ((u32)seg << 4) + TDB_PSP, 0xFF, 0)) {
        if (pdb)
            dpmi_free_desc(pdb);
        GlobalFree(xsel);
        GlobalFree(t);
        return 0;
    }
    alias = galias_code(t, pdb);
    if (!alias) {
        dpmi_free_desc(pdb);
        GlobalFree(xsel);
        GlobalFree(t);
        return 0;
    }

    /* the PDB: a child PSP of KERNEL's (DOS 55h), as observed: its parent
       field and GetCurrentPDB's DX are KERNEL's PSP, the environment is
       KERNEL's (one selector for every task), the command tail is the one
       given, and the first FCB holds 2 and nCmdShow (Pietrek; observed
       for the first task) */
    set_psp(psp_sel);
    rm_int(0x21, 0x5500, 0, seg + (TDB_PSP >> 4), *(u16 FAR *)MK_FP(psp_sel, 2));
    p = MK_FP(pdb, 0);
    *(u16 FAR *)(p + 0x16) = psp_sel;
    *(u16 FAR *)(p + 0x2C) = *(u16 FAR *)MK_FP(psp_sel, 0x2C);
    n = tail ? tail[0] : 0;
    if (n > 0x7E)
        n = 0x7E;
    p[0x80] = (u8)n;
    for (i = 0; i < n; i++)
        p[0x81 + i] = tail[1 + i];
    p[0x81 + n] = '\r';
    *(u16 FAR *)(p + 0x5C) = show ? show[0] : 2;
    *(u16 FAR *)(p + 0x5E) = show ? show[1] : 1;   /* UNVERIFIED w/o */
    if (cur_task)
        set_psp(cur_pdb);

    /* the TDB */
    TW(t, TDB_EVENTS) = 0;
    TW(t, TDB_SELF) = t;
    TW(t, TDB_FPCW) = 0x037F;           /* observed */
    TW(t, TDB_VERSION) = *(u16 FAR *)MK_FP(m->ne_sel, 0x3E);
    TW(t, TDB_HINST) = dg_hnd;
    TW(t, TDB_HMOD) = m->ne_sel;
    TW(t, TDB_PARENT) = cur_task;       /* UNVERIFIED for the first task */
    TW(t, TDB_PDB) = pdb;
    TW(t, TDB_DTA) = 0x80;
    TW(t, TDB_DTA + 2) = pdb;
    save_dir(t);
    TW(t, TDB_LIBINIT) = libinit;
    TW(t, TDB_ALIAS) = alias;
    procinst_init_tdb(t);
    for (i = 0; i < 8 && m->name[i]; i++)
        TB(t, TDB_NAME + i) = (u8)m->name[i];
    TW(t, TDB_SIG) = TDB_MAGIC;

    /* the start-up frame on the task's stack (kentry.asm, task_switch and
       task_tramp): its entry point, then the registers a task starts
       with, observed on Windows 3.1: AX = 0, BX = stack size, CX = heap
       size, DX = 0, SI = hPrevInstance, DI = hInstance, BP = 0, ES = PDB,
       DS = SS = DGROUP, SP = the end of the stack (static data, then the
       stack, then the local heap) */
    ds = MOD_SEG(m, h->auto_data - 1);
    dsize = (ds->min_alloc ? ds->min_alloc : 0x10000UL) + h->stack_size;
    if (dsize > 0xFFFEUL)
        dsize = 0xFFFEUL;
    sp = h->sp ? h->sp : (u16)(dsize & ~1UL);
    {
        u16 FAR *s = MK_FP(dg_sel, sp - 24);
        s[11] = m->seg[h->cs - 1].sel;  /* entry CS:IP */
        s[10] = h->ip;
        s[9] = FP_SEG((void FAR *)task_tramp);
        s[8] = FP_OFF((void FAR *)task_tramp);
        s[7] = 0;                       /* BP */
        s[6] = dg_sel;                  /* DS */
        s[5] = pdb;                     /* ES */
        s[4] = hprev;                   /* SI */
        s[3] = dg_hnd;                  /* DI */
        s[2] = h->stack_size;           /* BX */
        s[1] = h->heap_size;            /* CX */
        s[0] = 0;                       /* DX */
    }
    TW(t, TDB_SSSP) = sp - 24;
    TW(t, TDB_SSSP + 2) = dg_sel;

    x->self = xsel;
    x->htask = t;
    x->m = m;
    x->dg_sel = dg_sel;
    x->dg_hnd = dg_hnd;
    x->stack_size = h->stack_size;
    x->heap_size = h->heap_size;
    x->sp = sp;
    x->hprev = hprev;
    x->next = tx_first;
    tx_first = x;

    list_insert(t);
    num_tasks++;
    TW(t, TDB_EVENTS) = 1;
    return t;
}

static u16 seg_limit(u16 sel);
#pragma aux seg_limit = ".286p" "lsl ax, dx" parm [dx] value [ax];

/* Give task t a copy of the environment at env:0 (LoadModule's parameter
   block). Observed: the task gets a new block (32 bytes for "TK16=YES"
   and the double NUL), not the selector given, and the block was still
   there after the task had ended. UNVERIFIED: what follows the double
   NUL (nothing here; DOS would add the program's path); when Windows
   frees the block (here: never). */
void task_set_env(u16 t, u16 env)
{
    u32 lim = gsize_of_sel(env);
    u16 n = 0, sel;
    const u8 FAR *e = MK_FP(env, 0);
    if (!lim)
        lim = (u32)seg_limit(env) + 1;
    while ((u32)n + 1 < lim && n < 0x7FFF && (e[n] || e[n + 1]))
        n++;
    if (!galloc(GMEM_FIXED, n + 2, 0, &sel))
        return;
    kmemcpy(MK_FP(sel, 0), e, n + 2 <= lim ? n + 2 : (u16)lim);
    *(u16 FAR *)MK_FP(TW(t, TDB_PDB), 0x2C) = sel;
}

/* Start the new task t from a running task: it runs until it yields
   (observed: LoadModule returned after the new task's first Yield). */
void task_start(u16 t)
{
    (void)t;
    if (cur_task)
        OldYield();
}

/* Run the tasks (from KERNEL's own stack, after the first task is made);
   returns the exit code of the last one when no task is left. */
u16 task_boot(void)
{
    u16 t;
    in_sched = 1;
    for (t = task_head; t && !TW(t, TDB_EVENTS); t = TW(t, TDB_NEXT))
        ;
    if (!t)
        return 0xFF;
    list_requeue(t);
    cur_task = t;
    cur_pdb = TW(t, TDB_PDB);
    set_psp(cur_pdb);
    restore_dir(t, 0);
    in_sched = 0;
    task_switch((u16 FAR *)boot_sssp, TW(t, TDB_SSSP + 2), TW(t, TDB_SSSP));
    /* back here when the last task has ended */
    set_psp(psp_sel);
    return last_exit;
}

/* ---- ending a task ---- */

/* INT 21h AH=4Ch from a task (kentry.asm), on KERNEL's exit stack.
   Observed on Windows 3.1 when a task ends: the blocks it allocated, its
   DGROUP and (with its last instance) its module and the DLLs it imports
   are freed, a DLL it loaded with LoadLibrary stays; the other tasks keep
   their directories. Order as in Pietrek: free the task's blocks, unlink
   it, close its files, free its module, then its PDB and TDB. */
void __cdecl task_exit_c(u16 code)
{
    u16 t = cur_task, pdb = cur_pdb, i;
    taskx FAR *x = tx_of(t), FAR *FAR *pp;

    module FAR *m = x ? x->m : NULL;

    if (boot_tdb && t == boot_tdb)
        kexit_to_dos((u8)code);         /* while booting: back to DOS */

    in_sched = 1;
    if (TW(t, TDB_LIBINIT))
        GlobalFree(TW(t, TDB_LIBINIT));
    gfree_owner(pdb);                   /* also the alias, thunk segments,
                                           the list of DLLs to initialise */
    list_unlink(t);
    num_tasks--;
    for (i = 0; i < *(u16 FAR *)MK_FP(pdb, 0x32); i++) {
        regs16 r;
        r.ax = 0x3E00;
        r.bx = i;
        r.ds = r.es = 0;
        dos21(&r);
    }
    for (pp = &tx_first; *pp; pp = &(*pp)->next)
        if (*pp == x) {
            *pp = x->next;
            break;
        }
    in_sched = 0;
    cur_task = 0;
    if (m)
        module_end_instance(m, x->dg_hnd, x->dg_sel);
    set_psp(psp_sel);
    cur_pdb = 0;
    dpmi_free_desc(pdb);
    if (x)
        GlobalFree(x->self);
    GlobalFree(t);
    last_exit = code;
    in_sched = 1;
    if (!num_tasks) {
        /* No task left: back to KERNEL's start-up, which ends (on
           Windows the last task's end calls ExitWindows, Pietrek). */
        task_switch(NULL, boot_sssp[1], boot_sssp[0]);
    }
    for (;;) {
        for (t = task_head; t && !TW(t, TDB_EVENTS); t = TW(t, TDB_NEXT))
            ;
        if (t)
            break;
        idle();
    }
    switch_to(t);                       /* does not return */
}

/*
 * InitTask (KERNEL.91), register based.
 * In:  BX = stack size, CX = heap size, DI = hInstance, SI = hPrevInstance,
 *      ES = PDB, DS = DGROUP (as at task entry).
 * Out (observed on Windows 3.1): AX = the PDB, ES:BX = command line (NUL
 *      instead of the CR, past its leading blanks), CX = stack limit
 *      (entry SP - stack size + 96h), DX = nCmdShow (LoadModule's or
 *      WinExec's), SI = hPrevInstance, DI = hInstance. Instance words 0Ah
 *      = CX, 0Ch = 0Eh = entry SP. With a heap size, the local heap is
 *      made in the last CX bytes of DGROUP (as LocalInit(DS, 0, CX));
 *      pHeap goes to instance word 06h. Then the implicit DLLs loaded for
 *      this task are initialised, one after the other on its stack.
 * UNVERIFIED: InitTask's result when the heap cannot be made; what
 * happens when a DLL's initialisation fails (Pietrek: the task ends with
 * exit code F0h, done here).
 */
void __cdecl inittask_c(regs16 FAR *r)
{
    u16 FAR *inst = MK_FP(r->ds, 0);
    taskx FAR *x = tx_of(cur_task);
    u16 pdb = cur_pdb;
    u8 FAR *psp = MK_FP(pdb, 0);
    u16 sp, top, cmd, end, lib;

    if (!x) {
        r->ax = 0;
        return;
    }
    sp = x->sp;
    top = sp > x->stack_size ? sp - x->stack_size + STACK_SLOP : 0;
    inst[5] = top;              /* 0Ah: stack top (lowest address) */
    inst[6] = sp;               /* 0Ch: lowest SP seen so far */
    inst[7] = sp;               /* 0Eh: stack bottom (highest address) */

    end = 0x81 + (psp[0x80] < 0x7E ? psp[0x80] : 0x7E);
    psp[end] = 0;
    for (cmd = 0x81; cmd < end && (psp[cmd] == ' ' || psp[cmd] == '\t'); cmd++)
        ;

    r->ax = pdb;
    if (x->heap_size && !local_init_task(r->ds, x->heap_size))
        r->ax = 0;
    r->bx = cmd;
    r->cx = top;
    r->dx = *(u16 FAR *)(psp + 0x5C) == 2 ? *(u16 FAR *)(psp + 0x5E) : 1;
    r->si = x->hprev;
    r->di = x->dg_hnd;
    r->es = pdb;

    lib = TW(cur_task, TDB_LIBINIT);
    if (lib) {
        u16 n = *(u16 FAR *)MK_FP(lib, 0), i;
        TW(cur_task, TDB_LIBINIT) = 0;
        for (i = 0; i < n; i++)
            if (!module_init_dll(*(u16 FAR *)MK_FP(lib, 2 + 2 * i))) {
                GlobalFree(lib);
                dos(0x4CF0, 0, 0);      /* ends the task */
            }
        GlobalFree(lib);
    }
}

/* ---- API ---- */

void KAPI FatalExit(u16 code)                                 /* KERNEL.1 */
{
    regs16 r;
    kputs("w16: FatalExit ");
    kputhex(code);
    kputs("\r\n");
    r.ax = 0x4CFF;              /* ends the task through our INT 21h hook */
    r.ds = r.es = 0;
    dos21(&r);
}

/* Low word: Windows version, major in the low byte (3.10 -> 0A03h).
   High word: DOS version, major in the high byte. */
u32 KAPI GetVersion(void)                                     /* KERNEL.3 */
{
    regs16 r;
    r.ax = 0x3000;
    r.ds = r.es = 0;
    dos21(&r);
    return ((u32)(((r.ax & 0xFF) << 8) | (r.ax >> 8)) << 16) | 0x0A03;
}

/* Without a message queue (no USER yet) Yield is OldYield (Pietrek). */
void KAPI Yield(void)                                         /* KERNEL.29 */
{
    if (cur_task)
        TW(cur_task, TDB_YIELDTO) = 0;
    OldYield();
}

/* Observed: returns 0; takes one event, and waits for one while there is
   none. UNVERIFIED: the result after waiting. */
u16 KAPI WaitEvent(u16 htask)                                 /* KERNEL.30 */
{
    u16 t = htask ? task_of(htask) : cur_task;
    if (!t)
        return 0;
    for (;;) {
        if (TW(t, TDB_EVENTS)) {
            TW(t, TDB_EVENTS)--;
            return 0;
        }
        if (t != cur_task || t == boot_tdb)
            return 0;           /* UNVERIFIED: another task's, the boot's */
        reschedule();
    }
}

/* Observed: one event more for the task (0: the caller); AX afterwards
   held the hTask. */
u16 KAPI PostEvent(u16 htask)                                 /* KERNEL.31 */
{
    u16 t = htask ? task_of(htask) : cur_task;
    if (t)
        TW(t, TDB_EVENTS)++;
    return t;
}

/* Observed: returns the previous queue; 0 means the current task. */
u16 KAPI SetTaskQueue(u16 htask, u16 q)                       /* KERNEL.34 */
{
    u16 t = htask ? task_of(htask) : cur_task, old;
    if (!t)
        return 0;
    old = TW(t, TDB_QUEUE);
    TW(t, TDB_QUEUE) = q;
    return old;
}

u16 KAPI GetTaskQueue(u16 htask)                              /* KERNEL.35 */
{
    u16 t = htask ? task_of(htask) : cur_task;
    return t ? TW(t, TDB_QUEUE) : 0;
}

/* Observed: AX = the current task, DX = the first task in the list. */
u32 KAPI GetCurrentTask(void)                                 /* KERNEL.36 */
{
    return ((u32)task_head << 16) | cur_task;
}

/* AX = the current task's PDB, DX = KERNEL's own PSP (observed: also
   what a task's PDB has as its parent). */
u32 KAPI GetCurrentPDB(void)                                  /* KERNEL.37 */
{
    return ((u32)psp_sel << 16) | (cur_task ? cur_pdb : psp_sel);
}

void KAPI OutputDebugString(const char FAR *s)                /* KERNEL.115 */
{
    if (s)
        kputs(s);
}

/* Pietrek: an event for the caller while the scheduler looks, so it
   runs again when no other task has one. */
void KAPI OldYield(void)                                      /* KERNEL.117 */
{
    u16 t = cur_task;
    if (!t || in_sched)
        return;
    TW(t, TDB_EVENTS)++;
    reschedule();
    TW(t, TDB_EVENTS)--;
}

/* GetDOSEnvironment: the environment (every task shares KERNEL's). */
u32 KAPI GetDOSEnvironment(void)                              /* KERNEL.131 */
{
    u16 pdb = cur_task ? cur_pdb : psp_sel;
    return (u32)*(u16 FAR *)MK_FP(pdb, 0x2C) << 16;
}

u32 KAPI GetWinFlags(void)                                    /* KERNEL.132 */
{
    return get_winflags();
}

/* Observed: with an event for the task, it runs next; without one,
   nothing happens (TDB AAh is 0 afterwards). */
void KAPI DirectedYield(u16 htask)                            /* KERNEL.150 */
{
    if (cur_task)
        TW(cur_task, TDB_YIELDTO) = task_of(htask);
    OldYield();
}

u16 KAPI GetNumTasks(void)                                    /* KERNEL.152 */
{
    return num_tasks;
}

/* Observed: the hInstance (even), not DS. */
u16 KAPI GetTaskDS(void)                                      /* KERNEL.155 */
{
    return cur_task ? TW(cur_task, TDB_HINST) : 0;
}

/* Observed: the argument itself when it is a task's selector (whatever
   its RPL bits: hTask & ~1 came back as hTask & ~1), else 0 (the
   hInstance, hModule, PDB and 0 give 0). */
u16 KAPI IsTask(u16 h)                                        /* KERNEL.320 */
{
    return task_of(h) ? h : 0;
}

u32 KAPI GetSelectorBase(u16 sel)                             /* KERNEL.186 */
{
    return dpmi_get_base(sel);
}

/* ---- the boot task ----
   While Windows boots, KERNEL runs as a task of its own, so that what
   the boot modules do for "the current task" works (USER makes a message
   queue for it: SetTaskQueue(0, ...)): Pietrek (ch. 1, BootStrap) shows
   KERNEL making a TDB with KERNEL's PDB, DTA PDB:80h, one event, module
   -1 and its signature, and deleting it once the shell is loaded
   (SlowBoot). w16's is not in the task list: nothing is scheduled until
   it is gone. UNVERIFIED: the fields Pietrek does not list. */
u16 task_boot_tdb(void)
{
    u16 seg, t = galloc_dos(TDB_SIZE, 0, &seg);
    if (!t)
        return 0;
    TW(t, TDB_SELF) = t;
    TW(t, TDB_EVENTS) = 1;
    TW(t, TDB_PDB) = psp_sel;
    TW(t, TDB_DTA) = 0x80;
    TW(t, TDB_DTA + 2) = psp_sel;
    TW(t, TDB_HMOD) = 0xFFFF;
    TW(t, TDB_VERSION) = NE_HDR(module_first)->expver;
    TW(t, TDB_SIG) = TDB_MAGIC;
    boot_tdb = t;
    cur_task = t;
    cur_pdb = psp_sel;
    return t;
}

void task_boot_tdb_end(void)
{
    u16 t = boot_tdb;
    if (!t)
        return;
    if (cur_task == t) {
        cur_task = 0;
        cur_pdb = 0;
    }
    boot_tdb = 0;
    GlobalFree(t);
}
