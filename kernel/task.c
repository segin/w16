/*
 * task.c - task start-up and the first task-related APIs.
 *
 * M1: a single task, no scheduler. The task database (TDB), per-task PSP,
 * and Yield/WaitEvent scheduling come with USER in M2.
 */
#include "kernel.h"

u16 psp_sel;

/* InitTask's stack limit lies this far above the lowest address of the
   stack (observed on Windows 3.1: SP - limit = stack size - 96h). */
#define STACK_SLOP      0x0096

#define SW_SHOWNORMAL   1

static struct {
    module FAR *m;
    u16 hinstance;
    u16 stack_size;
    u16 heap_size;
    u16 sp;                     /* SP at task entry */
} cur;

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
    return task_active ? cur.m : NULL;
}

u16 task_start(module FAR *m, const char FAR *cmdtail)
{
    ne_header FAR *h = NE_HDR(m);
    const ne_segment FAR *ds;
    u16 dsel, ssel, sp;
    u32 dsize;

    (void)cmdtail;
    if (!h->auto_data || h->auto_data > m->seg_count ||
        !h->cs || h->cs > m->seg_count || !h->ss || h->ss > m->seg_count) {
        kputs("w16: not a runnable application\r\n");
        return 0xFE;
    }
    ds = MOD_SEG(m, h->auto_data - 1);
    /* Layout of the automatic data segment (observed on Windows 3.1):
       static data, then the stack, then the local heap. SP starts at the
       end of the stack. */
    dsize = (ds->min_alloc ? ds->min_alloc : 0x10000UL) + h->stack_size;
    if (dsize > 0xFFFEUL)
        dsize = 0xFFFEUL;

    dsel = m->seg[h->auto_data - 1].sel;
    ssel = m->seg[h->ss - 1].sel;
    /* SP of 0 in the header means "end of the stack" (see above). */
    sp = h->sp ? h->sp : (u16)(dsize & ~1UL);
    /* UNVERIFIED: offset 18h of the module table (initial SS:SP) held this
       SP on Windows 3.1 in one probe build and other values in two
       earlier ones (ARCHITECTURE.md 9.2); w16 leaves the file's value. */

    cur.m = m;
    cur.hinstance = m->seg[h->auto_data - 1].hnd;
    cur.stack_size = h->stack_size;
    cur.heap_size = h->heap_size;
    cur.sp = sp;
    return task_run(m->seg[h->cs - 1].sel, h->ip, dsel, ssel, sp,
                    h->stack_size, h->heap_size, cur.hinstance, 0, psp_sel);
}

/*
 * InitTask (KERNEL.91), register based.
 * In:  BX = stack size, CX = heap size, DI = hInstance, SI = hPrevInstance,
 *      ES = PSP, DS = DGROUP (as at task entry).
 * Out: AX = 1 on success, ES:BX = command line, CX = stack limit,
 *      DX = nCmdShow, SI = hPrevInstance, DI = hInstance.
 * Fills the instance data at DGROUP:0000 (stack limits).
 * Observed on Windows 3.1 (Standard and 386 Enhanced): CX = 0Ah = entry SP
 * - stack size + STACK_SLOP, 0Ch = 0Eh = entry SP; the command tail is
 * NUL-terminated (the CR replaced) and BX points past its leading blanks.
 * With a nonzero heap size (CX at task entry), the task's local heap is
 * made in the last CX bytes of DGROUP (observed: the layout of
 * LocalInit(DS, 0, CX)); pHeap goes to instance word 06h.
 * UNVERIFIED: InitTask's result when that fails.
 */
void __cdecl inittask_c(regs16 FAR *r)
{
    u16 FAR *inst = MK_FP(r->ds, 0);
    u8 FAR *psp = MK_FP(psp_sel, 0);
    u16 sp = cur.sp;
    u16 top = sp > cur.stack_size ? sp - cur.stack_size + STACK_SLOP : 0;
    u16 cmd, end;

    inst[5] = top;              /* 0Ah: stack top (lowest address) */
    inst[6] = sp;               /* 0Ch: lowest SP seen so far */
    inst[7] = sp;               /* 0Eh: stack bottom (highest address) */

    end = 0x81 + (psp[0x80] < 0x7E ? psp[0x80] : 0x7E);
    psp[end] = 0;
    for (cmd = 0x81; cmd < end && (psp[cmd] == ' ' || psp[cmd] == '\t'); cmd++)
        ;

    r->ax = 1;
    if (cur.heap_size && !local_init_task(r->ds, cur.heap_size))
        r->ax = 0;
    r->bx = cmd;
    r->cx = top;
    r->dx = SW_SHOWNORMAL;
    r->si = 0;
    r->di = cur.hinstance;
    r->es = psp_sel;
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

void KAPI Yield(void)                                         /* KERNEL.29 */
{
}

u16 KAPI WaitEvent(u16 htask)                                 /* KERNEL.30 */
{
    (void)htask;
    return 0;
}

void KAPI OutputDebugString(const char FAR *s)                /* KERNEL.115 */
{
    if (s)
        kputs(s);
}

u32 KAPI GetWinFlags(void)                                    /* KERNEL.132 */
{
    return get_winflags();
}
