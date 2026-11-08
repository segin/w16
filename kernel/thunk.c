/*
 * thunk.c - stand-ins for imports that cannot be resolved.
 *
 * Each thunk is 8 bytes of code:   B8 lo hi        mov ax, <index>
 *                                  EA ofs seg      jmp far unimpl_entry
 * unimpl_entry (kentry.asm) reports the call and ends the task with exit
 * code 0FFh. Loading never fails just because an API is missing; the gap
 * shows up only if the program actually calls it.
 *
 * Thunks live in blocks of TB_COUNT, chained; another block is made when
 * all are used. A block holds the thunks' code (run through a code alias)
 * followed by their descriptions. The index is 16 bits (the immediate of
 * "mov ax"), which bounds the number of distinct missing imports.
 */
#include "kernel.h"

#define TB_COUNT    64          /* thunks per block */
#define THUNK_SIZE  8
#define NAME_MAX    32          /* longest import name reported */

typedef struct thunkinfo {
    char mod[MODNAME_MAX + 1];
    u16  ordinal;               /* 0 when imported by name */
    char name[NAME_MAX + 1];
    u32  real;                  /* a trace thunk's target, else 0 */
} thunkinfo;

typedef struct tblock {
    u8  code[TB_COUNT * THUNK_SIZE];
    thunkinfo info[TB_COUNT];
    u16 next;                   /* data selector of the next block */
    u16 code_sel;               /* code alias of this block */
} tblock;

static u16 first;               /* data selector of the first block */
static u16 count;               /* thunks made */

void thunk_init(void)
{
    first = 0;
    count = 0;
}

static tblock FAR *block(u16 sel)
{
    return MK_FP(sel, 0);
}

/* The block holding thunk `index`, made if needed (and allowed). */
static tblock FAR *block_of(u16 index, int make)
{
    u16 sel = first, prev = 0, n = index / TB_COUNT;
    while (sel && n) {
        prev = sel;
        sel = block(sel)->next;
        n--;
    }
    if (!sel && make && !n) {
        u16 cs;
        if (!galloc(GMEM_FIXED, sizeof(tblock), 0, &sel))
            return NULL;
        cs = dpmi_alloc_desc(1);
        if (!cs || !dpmi_set_desc(cs, dpmi_get_base(sel), sizeof(tblock) - 1, 1)) {
            if (cs)
                dpmi_free_desc(cs);
            GlobalFree(sel);
            return NULL;
        }
        block(sel)->code_sel = cs;
        if (prev)
            block(prev)->next = sel;
        else
            first = sel;
    }
    return sel ? block(sel) : NULL;
}

static int make(const char FAR *mod, u16 ordinal, const u8 FAR *pname,
                u16 *sel, u16 *off, u32 real);

int thunk_make(const char FAR *mod, u16 ordinal, const u8 FAR *pname,
               u16 *sel, u16 *off)
{
    return make(mod, ordinal, pname, sel, off, 0);
}

/* A thunk that records each call (trace_note) and goes on to real. */
int thunk_trace(const char FAR *mod, u16 ordinal, u16 *sel, u16 *off)
{
    u32 real = ((u32)*sel << 16) | *off;
    return make(mod, ordinal, NULL, sel, off, real);
}

static int make(const char FAR *mod, u16 ordinal, const u8 FAR *pname,
                u16 *sel, u16 *off, u32 real)
{
    u16 i, n;
    u8 FAR *code;
    char name[NAME_MAX + 1];
    tblock FAR *b;
    thunkinfo FAR *t;
    void (__far *target)(void) = real ? trace_entry : unimpl_entry;

    name[0] = 0;
    if (pname) {
        n = pname[0] < NAME_MAX ? pname[0] : NAME_MAX;
        for (i = 0; i < n; i++)
            name[i] = (char)pname[1 + i];
        name[n] = 0;
    }
    for (i = 0; i < count; i++) {
        b = block_of(i, 0);
        t = &b->info[i % TB_COUNT];
        if (t->ordinal == ordinal && kstricmp(t->mod, mod) == 0 &&
            kstricmp(t->name, name) == 0 && t->real == real)
            goto found;
    }
    if (count == 0xFFFF)
        return 0;
    i = count;
    b = block_of(i, 1);
    if (!b)
        return 0;
    count++;
    t = &b->info[i % TB_COUNT];
    for (n = 0; n < MODNAME_MAX && mod[n]; n++)
        t->mod[n] = mod[n];
    t->mod[n] = 0;
    t->ordinal = ordinal;
    kmemcpy(t->name, name, sizeof name);
    t->real = real;
    code = &b->code[(i % TB_COUNT) * THUNK_SIZE];
    code[0] = 0xB8;
    code[1] = (u8)i;
    code[2] = (u8)(i >> 8);
    code[3] = 0xEA;
    code[4] = (u8)FP_OFF(target);
    code[5] = (u8)(FP_OFF(target) >> 8);
    code[6] = (u8)FP_SEG(target);
    code[7] = (u8)(FP_SEG(target) >> 8);
found:
    *sel = b->code_sel;
    *off = (i % TB_COUNT) * THUNK_SIZE;
    return 1;
}

void __cdecl unimpl_report(u16 index)
{
    tblock FAR *b = index < count ? block_of(index, 0) : NULL;
    kputs("w16: call to unimplemented ");
    if (b) {
        thunkinfo FAR *t = &b->info[index % TB_COUNT];
        kputs(t->mod);
        kputs(".");
        if (t->name[0])
            kputs(t->name);
        else
            kputdec(t->ordinal);
    }
    kputs("\r\n");
}

void where(u16 sel);             /* fault.c */

/* ---- the import trace (a debugging aid for the Windows boot) ----
   SYSTEM.INI [w16] trace=MOD.ORD,MOD.ORD,... makes the loader bind those
   imports to trace thunks: each call is recorded (thunk, caller CS:IP,
   AX, BX, CX, DX and the first 6 stack words above the return address)
   in a ring the fault and watchdog reports print. */
#define TR_N 32
typedef struct trrec { u16 idx, ip, cs, ax, bx, cx, dx, arg[6]; } trrec;
static trrec trring[TR_N];
static u16 trpos, trcount;

/* called by trace_entry (kentry.asm) with the thunk's index and the
   caller's frame (return IP, CS, arguments) and registers; returns the
   real target */
u32 __cdecl trace_note(u16 idx, u16 FAR *frame, u16 ax, u16 bx, u16 cx, u16 dx)
{
    tblock FAR *b = block_of(idx, 0);
    trrec *r = &trring[trpos];
    u16 i;
    trpos = (trpos + 1) % TR_N;
    trcount++;
    r->idx = idx;
    r->ip = frame[0];
    r->cs = frame[1];
    r->ax = ax;
    r->bx = bx;
    r->cx = cx;
    r->dx = dx;
    for (i = 0; i < 6; i++)
        r->arg[i] = frame[2 + i];
    return b ? b->info[idx % TB_COUNT].real : 0;
}

void trace_print(void)
{
    u16 i, j, k = trpos;
    if (!trcount)
        return;
    kputs("traced calls (oldest first), ");
    kputhex(trcount);
    kputs(" in all:\r\n");
    for (i = 0; i < TR_N; i++, k = (k + 1) % TR_N) {
        trrec *r = &trring[k];
        tblock FAR *b;
        if (!r->cs)
            continue;
        b = block_of(r->idx, 0);
        kputs(" ");
        if (b) {
            kputs(b->info[r->idx % TB_COUNT].mod);
            kputs(".");
            kputdec(b->info[r->idx % TB_COUNT].ordinal);
        }
        kputs(" from ");
        kputhex(r->cs);
        kputs(":");
        kputhex(r->ip);
        where(r->cs);
        kputs(" ax="); kputhex(r->ax);
        kputs(" bx="); kputhex(r->bx);
        kputs(" cx="); kputhex(r->cx);
        kputs(" dx="); kputhex(r->dx);
        kputs(" args");
        for (j = 0; j < 6; j++) {
            kputs(" ");
            kputhex(r->arg[j]);
        }
        kputs("\r\n");
    }
}
