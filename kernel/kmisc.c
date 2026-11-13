/*
 * kmisc.c - KERNEL exports that Microsoft's drivers, GDI, USER, SHELL and
 * PROGMAN import (tools/kimports.py; ARCHITECTURE.md 9.4), needed to boot
 * Windows 3.1 on w16's KERNEL.
 *
 * Sources: the Windows 3.1 SDK (Programmer's Reference vol. 2) for the
 * documented functions; Schulman et al., "Undocumented Windows" (1992)
 * and Pietrek, "Windows Internals" (1993) for the undocumented ones and
 * for the TDB fields (Pietrek, Table 3-2); names from Wine's X11-licensed
 * spec files (wine-20020228). Facts only, rewritten. None of these has
 * been checked with a probe on Windows 3.1 yet unless noted: UNVERIFIED
 * throughout, in particular return values in odd cases.
 */
#include "kernel.h"

u16  KAPI GlobalAlloc(u16 flags, u32 size);
void FAR * KAPI GlobalLock(u16 h);
u16  KAPI GlobalUnlock(u16 h);
u32  local_spaces(u16 seg);

#define TDB_ERRMODE     0x18    /* Pietrek: error handling flags */
#define TDB_VERSION     0x1A
#define TDB_HMOD        0x1E
#define TDB_USERSIG     0x2A    /* USER's signal procedure */
#define TDB_COMPAT      0x4E    /* compatibility flags (dword) */
#define TW(t, o)        (*(u16 FAR *)MK_FP((t), (o)))
#define TD(t, o)        (*(u32 FAR *)MK_FP((t), (o)))

static u16 boot_errmode;        /* while booting there is no task */
static u16 locked_task;
static u32 filecdr_proc;
static u16 filecdr_task;

/* ExitKernel (KERNEL.2): leave Windows, back to DOS. USER calls it at the
   end of ExitWindows (Pietrek). UNVERIFIED: the exit code. */
void KAPI ExitKernel(void)
{
    win_screen = 0;                     /* USER's ExitWindows disabled the
                                           display: text mode */
    trace_print();                      /* [w16] trace, if any */
    kexit_to_dos(0);
}

/* LockCurrentTask (KERNEL.33): Schulman: lock (nonzero) or unlock the
   current task; IsTaskLocked returns the locked task. w16's scheduler
   does not honour it yet. */
u16 KAPI LockCurrentTask(u16 lock)
{
    locked_task = lock ? cur_task : 0;
    return locked_task;
}

u16 KAPI IsTaskLocked(void)                                   /* KERNEL.122 */
{
    return locked_task;
}

/* SetTaskSignalProc (KERNEL.38): USER's signal procedure of a task (TDB
   2Ah, Pietrek); returns the previous one. */
u32 KAPI SetTaskSignalProc(u16 htask, u32 proc)
{
    u32 old;
    if (!htask)
        htask = cur_task;
    if (!htask)
        return 0;
    old = TD(htask, TDB_USERSIG);
    TD(htask, TDB_USERSIG) = proc;
    return old;
}

/* DeletePathname (KERNEL.76): delete a file (Schulman: INT 21h 41h).
   UNVERIFIED: the result. */
u16 KAPI DeletePathname(const char FAR *path)
{
    regs16 r;
    r.ax = 0x4100;
    r.dx = FP_OFF(path);
    r.ds = FP_SEG(path);
    r.es = 0;
    r.bx = r.cx = r.si = r.di = 0;
    return dos21(&r) ? 0 : 1;
}

/* Reserved1 (KERNEL.77) = AnsiNext, Reserved2 (78) = AnsiPrev (Wine's
   names); SBCS: the next/previous character. */
char FAR * KAPI KAnsiNext(char FAR *p)
{
    return *p ? p + 1 : p;
}

char FAR * KAPI KAnsiPrev(char FAR *start, char FAR *p)
{
    return p > start ? p - 1 : start;
}

/* Reserved5 (KERNEL.87) = lstrcmp (Wine's name). UNVERIFIED: plain byte
   order here; USER's lstrcmp sorts by the language driver. */
int KAPI KLstrcmp(const char FAR *a, const char FAR *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

/* GetCodeInfo (KERNEL.104, SDK): lpProc is a procedure address or
   MAKELONG(segment number, hModule); fills a SEGINFO: offSegment
   (sectors), cbSegment, flags, cbAlloc, h, alignShift, reserved[2].
   Observed on 3.1 (TK25): the six words come from the module table's
   segment entry (loaded flags, the DGROUP's allocation with heap and
   stack), h is the segment's handle, reserved[] is left alone, a bad
   address gives 0 and writes nothing; the result is otherwise nonzero
   (various values). */
u16 KAPI GetCodeInfo(u32 proc, u16 FAR *info)
{
    u16 hi = (u16)(proc >> 16), lo = (u16)proc, i;
    module FAR *m = module_of(hi);
    ne_segment FAR *s;
    if (m && (m->ne_sel == hi || (hi | 1) == (m->ne_sel | 1))) {
        if (!lo || lo > m->seg_count)
            return 0;
        i = lo - 1;
    } else {
        for (m = module_first; m; m = m->next) {
            for (i = 0; i < m->seg_count; i++)
                if ((m->seg[i].sel | 7) == (hi | 7))
                    break;
            if (i < m->seg_count)
                break;
        }
        if (!m)
            return 0;
    }
    s = MOD_SEG(m, i);
    info[0] = s->sector;
    info[1] = s->file_len;
    info[2] = s->flags;
    info[3] = s->min_alloc;
    info[4] = MOD_SEGHND(m, i);
    info[5] = NE_HDR(m)->align_shift ? NE_HDR(m)->align_shift : 9;
    return 1;                   /* observed (TK25): reserved[] untouched */
}

/* GetExeVersion (KERNEL.105): the expected Windows version of the
   current task (TDB 1Ah); while booting, KERNEL's own (UNVERIFIED). */
u16 KAPI_ES GetExeVersion(void)
{
    if (cur_task)
        return TW(cur_task, TDB_VERSION);
    return NE_HDR(module_first)->expver;
}

/* SetErrorMode (KERNEL.107): the task's error mode (TDB 18h); returns
   the previous one. */
u16 KAPI_ES SetErrorMode(u16 mode)
{
    u16 old;
    if (!cur_task) {
        old = boot_errmode;
        boot_errmode = mode;
        return old;
    }
    old = TW(cur_task, TDB_ERRMODE);
    TW(cur_task, TDB_ERRMODE) = mode;
    return old;
}

/* GlobalPageLock / GlobalPageUnlock (191, 192): selector.c (as observed,
   tests/apps/tk17.c). GlobalWire/Fix and friends are in global.c. */

/* KbdRst (KERNEL.123), CVWBreak (205), DoSignal (139): khook.asm.
   Bunny_351 (351): Schulman: nothing. */
void KAPI Bunny351(void)
{
}

/* FileCdr (KERNEL.130): Schulman: install (or with NULL remove) the
   file-system change callback; fails if another task has one; -1L asks
   for the current one. */
u32 KAPI FileCdr(u32 proc)
{
    if (proc == 0xFFFFFFFFUL)
        return filecdr_proc;
    if (filecdr_proc && filecdr_task != cur_task)
        return 0;
    filecdr_proc = proc;
    filecdr_task = proc ? cur_task : 0;
    return 1;
}

/* GetHeapSpaces (KERNEL.138): Schulman: for an hModule or hInstance, the
   local heap's total (HIWORD) and free (LOWORD) bytes. Observed on 3.1
   (tests/apps/tk51.c, USER's heaps under KRNL286): "total" is the most
   the heap could span, FFFFh less the offset of its information block
   (word 6 of the segment: USER's DGROUP, pHeap AE4h: F51Bh; a heap in a
   430h-byte block, pHeap 24h: FFDBh), and "free" is that less the bytes
   in use. USER's GetFreeSystemResources computes its percentages from
   these and divided by zero (overflow) with w16's first version, which
   gave the heap's present span. */
u32 KAPI GetHeapSpaces(u16 h)
{
    module FAR *m = module_of(h);
    u16 ad, ph, total, used;
    u32 sp;
    if (m && (h | 1) == (m->ne_sel | 1)) {
        ad = NE_HDR(m)->auto_data;
        if (!ad || ad > m->seg_count)
            return 0;
        h = m->seg[ad - 1].sel;
    }
    h |= 1;
    sp = local_spaces(h);
    if (!sp)
        return 0;
    ph = *(u16 FAR *)MK_FP(h, 6);
    total = 0xFFFF - ph;
    used = (u16)(sp >> 16) - (u16)sp;
    return ((u32)total << 16) | (u16)(total - used);
}

/* GetCurPID (KERNEL.157): Schulman: returns 0 in Windows 3.x. */
u32 KAPI GetCurPID(u32 dw)
{
    (void)dw;
    return 0;
}

/* WinOldApCall (KERNEL.151): WINOLDAP.MOD's; no source describes it.
   TK34 (3.1, Standard mode): one word popped. 1 and 2 give AX=the
   argument and DX=a selector of KERNEL's PSP (base = the PSP, limit
   100h; WINOLDAP.MOD calls it with 1 first); 0 gives AX and DX
   selectors of real-mode memory after KERNEL's PSP (DX: base PSP+100h,
   limit FFFFh; AX: base PSP+1100h, limit 0Fh), meaning unknown (TODO:
   logged, 0 returned). */
u32 KAPI WinOldApCall(u16 fn)
{
    kdbg("w16: WinOldApCall ");
    kdbghex(fn);
    kdbg("\r\n");
    if (fn == 0)
        return 0;
    return ((u32)psp_sel << 16) | fn;
}

/* RegisterWinOldApHook (KERNEL.343) and GetWinOldApHooks (344):
   WINOLDAP.MOD's. TK34 (3.1): 343 pops three words and returns AX=1;
   344 takes none and returns DX:AX = 0 before any 343, afterwards a far
   pointer from 343's words (all words equal in the probe: which two is
   not known). TODO: logged; 343's last two words kept as the far
   pointer (UNVERIFIED which two). */
static u32 woa_hook;

u16 KAPI RegisterWinOldApHook(u16 a, u16 b, u16 c)
{
    kdbg("w16: RegisterWinOldApHook ");
    kdbghex(a);
    kdbg(" ");
    kdbghex(b);
    kdbg(" ");
    kdbghex(c);
    kdbg("\r\n");
    woa_hook = ((u32)b << 16) | c;
    return 1;
}

u32 KAPI GetWinOldApHooks(void)
{
    kdbg("w16: GetWinOldApHooks\r\n");
    return woa_hook;
}

/* IsWinOldApTask (KERNEL.158): w16 has no WINOLDAP yet. */
u16 KAPI IsWinOldApTask(u16 htask)
{
    (void)htask;
    return 0;
}


/* GetExpWinVer (KERNEL.167): the expected Windows version in the NE
   header (3Eh) of hModule or hInstance. */
u16 KAPI GetExpWinVer(u16 h)
{
    module FAR *m = module_of(h);
    return m ? NE_HDR(m)->expver : 0;
}

/* DirectResAlloc (KERNEL.168): SDK-era AllocResource without a
   resource: a moveable block of `size` bytes. UNVERIFIED: the flags and
   the owner. */
u16 KAPI DirectResAlloc(u16 hinst, u16 flags, u16 size)
{
    (void)hinst;
    (void)flags;
    return GlobalAlloc(GMEM_MOVEABLE, size);
}


/* LongPtrAdd (KERNEL.180): selector.c. Observed (tests/apps/tk17.c), it
   moves the selector's base rather than stepping by __AHINCR. */

/* WriteOutProfiles (KERNEL.315): w16 writes profiles through at once. */
void KAPI WriteOutProfiles(void)
{
}

/* IsRomModule (KERNEL.323): no ROM Windows here. */
u16 KAPI IsRomModule(u16 h)
{
    (void)h;
    return 0;
}

/* LogError (KERNEL.324), LogParamError (325): the debugging KERNEL
   reports these; the retail one passes them on to the notification hook
   (Schulman: 66h, 67h; toolhelp.c) and does nothing else (Pietrek).
   Observed (tests/apps/tk50.c, KRNL286): AX and DX come back as they
   went in; khook.asm's LOGERROR and LOGPARAMERROR keep them around these */
void KAPI LogError_(u16 err, void FAR *info)
{
    th_logerror(err, info);
}

void KAPI LogParamError_(u16 err, void FAR *proc, void FAR *param)
{
    th_logparamerror(err, proc, param);
}

/* ---- pointer validation (KERNEL.334-337, 346, 347; SDK) ---- */

static u16 lar(u16 sel);
#pragma aux lar = \
    "xor ax, ax" \
    "lar ax, dx" \
    "jz ok" \
    "xor ax, ax" \
    "ok:" \
    parm [dx] value [ax] modify exact [ax];


/* access rights byte (LAR's bits 8-15), 0 if not accessible */
static u8 rights(u16 sel)
{
    return sel ? (u8)(lar(sel) >> 8) : 0;
}

/* bytes [off, off + n) of sel readable (want 0) or writable (want 1) */
static int range_ok(u16 sel, u32 off, u32 n, int write)
{
    u8 ar = rights(sel);
    if (!(ar & 0x80) || !(ar & 0x10))
        return 0;                       /* not present, or a system one */
    if (ar & 0x08) {                    /* code */
        if (write || !(ar & 0x02))
            return 0;
    } else if (write && !(ar & 0x02)) {
        return 0;
    }
    if (n == 0)
        return 1;
    return off + n - 1 <= sel_lim16(sel);     /* UNVERIFIED: expand-down */
}

u16 KAPI_ES IsBadReadPtr(void FAR *p, u16 n)
{
    return !range_ok(FP_SEG(p), FP_OFF(p), n, 0);
}

u16 KAPI IsBadWritePtr(void FAR *p, u16 n)
{
    return !range_ok(FP_SEG(p), FP_OFF(p), n, 1);
}

u16 KAPI IsBadCodePtr(void FAR *p)
{
    u8 ar = rights(FP_SEG(p));
    return !((ar & 0x80) && (ar & 0x18) == 0x18 && FP_OFF(p) <= sel_lim16(FP_SEG(p)));
}

u16 KAPI IsBadStringPtr(const char FAR *p, u16 max)
{
    u16 sel = FP_SEG(p), off = FP_OFF(p), lim, i;
    if (!range_ok(sel, off, 1, 0))
        return 1;
    lim = sel_lim16(sel);
    for (i = 0; i < max; i++) {
        if ((u32)off + i > lim)
            return 1;
        if (!p[i])
            return 0;
    }
    return 0;
}

static u16 huge_bad(void FAR *p, u32 n, int write)
{
    u16 sel = FP_SEG(p);
    u32 off = FP_OFF(p);
    while (n) {
        u32 chunk = 0x10000UL - off;
        if (chunk > n)
            chunk = n;
        if (!range_ok(sel, off, chunk, write))
            return 1;
        n -= chunk;
        off = 0;
        sel += sel_inc;
    }
    return 0;
}

u16 KAPI IsBadHugeReadPtr(void FAR *p, u32 n)
{
    return huge_bad(p, n, 0);
}

u16 KAPI IsBadHugeWritePtr(void FAR *p, u32 n)
{
    return huge_bad(p, n, 1);
}

/* lstrcpyn (KERNEL.353, SDK): copy at most n - 1 characters and a NUL;
   returns dst. */
char FAR * KAPI lstrcpyn(char FAR *dst, const char FAR *src, u16 n)
{
    u16 i = 0;
    if (!n)
        return dst;
    for (; i + 1 < n && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return dst;
}

/* GetAppCompatFlags (KERNEL.354): Schulman: TDB 4Eh of the task (0: the
   current one). w16 does not fill that field yet, so 0. */
u32 KAPI_ES GetAppCompatFlags(u16 htask)
{
    if (!htask)
        htask = cur_task;
    return htask ? TD(htask, TDB_COMPAT) : 0;
}
