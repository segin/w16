/*
 * task.c - task start-up and the first task-related APIs.
 *
 * M1: a single task, no scheduler. The task database (TDB), per-task PSP,
 * and Yield/WaitEvent scheduling come with USER in M2.
 */
#include "kernel.h"

u16 psp_sel;

#define WF_PMODE        0x0001
#define WF_CPU286       0x0002
#define WF_CPU386       0x0004
#define WF_CPU486       0x0008
#define WF_STANDARD     0x0010
#define WF_ENHANCED     0x0020

#define SW_SHOWNORMAL   1

static struct {
    module *m;
    u16 hinstance;
    u16 stack_size;
    u16 heap_size;
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
    /* TODO: WF_80x87 (0x0400) when a coprocessor is present */
    return f;
}

u16 task_start(module *m, const char FAR *cmdtail)
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
    ds = (const ne_segment FAR *)MK_FP(m->ne_sel, h->seg_off) + (h->auto_data - 1);
    dsize = (ds->min_alloc ? ds->min_alloc : 0x10000UL) + h->heap_size + h->stack_size;
    if (dsize > 0x10000UL)
        dsize = 0x10000UL;

    dsel = m->seg_sel[h->auto_data - 1];
    ssel = m->seg_sel[h->ss - 1];
    /* SP of 0 in the header means "top of the automatic data segment".
       UNVERIFIED: exact top-of-stack value Windows uses. */
    sp = h->sp ? h->sp : (u16)(dsize & ~1UL);

    cur.m = m;
    cur.hinstance = m->seg_hnd[h->auto_data - 1];
    cur.stack_size = h->stack_size;
    cur.heap_size = h->heap_size;
    return task_run(m->seg_sel[h->cs - 1], h->ip, dsel, ssel, sp,
                    h->stack_size, h->heap_size, cur.hinstance, 0, psp_sel);
}

/*
 * InitTask (KERNEL.91), register based.
 * In:  BX = stack size, CX = heap size, DI = hInstance, SI = hPrevInstance,
 *      ES = PSP, DS = DGROUP (as at task entry).
 * Out: AX = 1 on success, ES:BX = command line, CX = stack limit,
 *      DX = nCmdShow, SI = hPrevInstance, DI = hInstance.
 * Fills the instance data at DGROUP:0000 (stack limits).
 * UNVERIFIED: BX = 81h, CX = stack top, the instance-data offsets, and the
 * stack-limit arithmetic. TODO: local heap (LocalInit) when CX != 0.
 */
void __cdecl inittask_c(regs16 FAR *r)
{
    u16 FAR *inst = MK_FP(r->ds, 0);
    /* The caller's SP before the far call: our regs16 block plus the
       4-byte return address sit on top of it. */
    u16 sp = FP_OFF(r) + sizeof(regs16) + 4;
    u16 top = sp > cur.stack_size ? sp - cur.stack_size : 0;

    inst[5] = top;              /* 0Ah: stack top (lowest address) */
    inst[6] = sp;               /* 0Ch: lowest SP seen so far */
    inst[7] = sp;               /* 0Eh: stack bottom (highest address) */

    r->ax = 1;
    r->bx = 0x81;
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
