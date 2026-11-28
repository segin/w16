/*
 * fault.c - while booting Windows, report a processor exception (which
 * module and segment faulted, and the registers) and leave to DOS,
 * instead of DOSX's "Fault outside of MS-DOS Extender". A debugging aid
 * for the drop-in boot (tests/dropin.sh); Windows' own KERNEL hooks these
 * exceptions too (Pietrek, ch. 1), to handle them. w16 handles one kind:
 * a fault inside a module's "__GP" range resumes at its recovery code
 * (gp_handler below); the rest are reported.
 */
#include "kernel.h"
#include "kfile.h"
#include "w16ver.h"

u32 KAPI GetProcAddress(u16 h, const char FAR *name);
u16 KAPI GetPrivateProfileInt(const char FAR *sec, const char FAR *key, u16 def,
                              const char FAR *file);
static const char sysini[] = "SYSTEM.INI";

extern u16 __cdecl fault_regs[16];
#define I21_N 64                /* INT 21h calls in the ring (kentry.asm) */
extern u16 __cdecl i21_ring[I21_N * 5];
extern u16 __cdecl i21_pos;
extern void __far fault_entry_00h(void);
extern void __far fault_entry_06h(void);
extern void __far fault_entry_0Bh(void);
extern void __far fault_entry_0Ch(void);
extern void __far fault_entry_0Dh(void);
extern void __far fault_entry_0Eh(void);
#pragma aux fault_entry_00h "*"
#pragma aux fault_entry_06h "*"
#pragma aux fault_entry_0Bh "*"
#pragma aux fault_entry_0Ch "*"
#pragma aux fault_entry_0Dh "*"
#pragma aux fault_entry_0Eh "*"

/* INT 10h in real mode (DPMI 0300h, the host's stack) with AX, BX and
   CX; rmc then holds what it returned. 0 when DPMI refused. */
static struct {
    u32 edi, esi, ebp, res, ebx, edx, ecx, eax;
    u16 flags, es, ds, fs, gs, ip, cs, sp, ss;
} rmc;

static int rm_int10(u16 ax, u16 bx, u16 cx)
{
    regs16 r;
    kmemset(&rmc, 0, sizeof rmc);
    kmemset(&r, 0, sizeof r);
    rmc.eax = ax;
    rmc.ebx = bx;
    rmc.ecx = cx;
    r.ax = 0x0300;
    r.bx = 0x10;
    r.di = FP_OFF((void FAR *)&rmc);
    r.es = FP_SEG((void FAR *)&rmc);
    return !dpmi31(&r);
}

/* Back to text mode before a report goes to the screen, as Windows'
   own KERNEL does before "Error loading ..." (Pietrek, TextMode: USER's
   DisableOEMLayer if it can, else INT 10h AX=0003h). w16 uses INT 10h
   (in real mode, through DPMI 0300h): KERNEL leaves to DOS right after,
   so the display driver's state is not needed any more. 1 when mode 3
   was set (not without a display driver: the screen is text already). */
u16 text_mode(void)
{
    win_screen = 0;                     /* (reports go to the screen now) */
    if (!winboot_active || !module_find("DISPLAY"))
        return 0;
    rm_int10(0x0003, 0, 0);
    return 1;
}

/* the screen's rows (INT 10h AX=1130h: DL = rows - 1, on an EGA or VGA;
   other adapters leave DL as it was, 0: 25) */
static u16 rpt_rows = 25;

static void rows_now(void)
{
    u16 n;
    rpt_rows = 25;
    if (rm_int10(0x1130, 0, 0)) {
        n = (u8)rmc.edx + 1;
        if (n > 25 && n <= 60)
            rpt_rows = n;
    }
}

/* The fault report in 80x50: after mode 3, the VGA's 8x8 ROM font (INT
   10h AX=1112h, BL=0), so the registers, the stack, its frames and the
   INT 21h calls fit on one screen. Only on a VGA (INT 10h AX=1A00h: AL
   1Ah, BL 7 or 8, the active display): another adapter stays at 25 rows
   (an EGA would give 43). DOS's prompt after the report stays in 50
   rows (MODE CO80, or any program's mode set, gives 25 again). */
static void rows50(void)
{
    if (rm_int10(0x1A00, 0, 0) && (u8)rmc.eax == 0x1A &&
        ((u8)rmc.ebx == 7 || (u8)rmc.ebx == 8)) {
        rm_int10(0x1112, 0x0000, 0);
        rm_int10(0x0100, 0, 0x0607);    /* the cursor in lines 6-7 of 8 */
    }
    rows_now();
}

static void hook(u8 n, void (__far *h)(void))
{
    regs16 r;
    r.ax = 0x0203;
    r.bx = n;
    r.cx = FP_SEG(h);
    r.dx = FP_OFF(h);
    r.ds = r.es = 0;
    dpmi31(&r);
}

static void rpt_init(void);

void fault_init(void)
{
    rpt_init();
    hook(0x00, fault_entry_00h);
    hook(0x06, fault_entry_06h);
    hook(0x0B, fault_entry_0Bh);
    hook(0x0C, fault_entry_0Ch);
    hook(0x0D, fault_entry_0Dh);
    hook(0x0E, fault_entry_0Eh);
}

static u16 lar_any(u16 sel);
#pragma aux lar_any = "lar ax, ax" "jz l1" "xor ax, ax" "l1:" parm [ax] value [ax];
static u16 lsl(u16 sel);
#pragma aux lsl = \
    "xor ax, ax" \
    "lsl ax, dx" \
    parm [dx] value [ax] modify exact [ax];

/* ---- "__GP" tables (Schulman, Undocumented Windows ch. 5, _GP and
   HasGPHandler): a module that validates parameters by letting a load
   fault exports "__GP", a table of { code selector, low offset, high
   offset, recovery offset } ending with a 0 selector; a fault at an
   offset from low up to (not including) high resumes at the recovery
   offset in the same segment. TK38 (3.1): KERNEL (35 entries, as
   KERNEL.342), USER (121), GDI (69) have one, DISPLAY none;
   HasGPHandler(seg:low ... seg:high-1) returns the recovery offset,
   seg:high and seg:low-1 give 0. TK40 (3.1, a stand-in module named
   PENWIN whose resident "__GP" covers a faulting load): the recovery
   code gets the fault's registers, DS, ES and SS, SP 4 lower with the
   faulting IP and then the error code pushed, flags 3202h
   (kentry.asm, fault_exit). Only the resident name is looked at. */
static u16 gp_find(u16 cs, u16 ip)
{
    module FAR *m;
    u16 i;
    u32 t;
    const u16 FAR *e;
    for (m = module_first; m; m = m->next)
        for (i = 0; i < m->seg_count; i++)
            if (m->seg[i].sel && (m->seg[i].sel | 7) == (cs | 7))
                goto found;
    return 0;
found:
    t = GetProcAddress(m->ne_sel, "__GP");
    if (!t)
        return 0;
    /* (each entry inside the segment before it is read: KERNEL audit,
       medium 19) */
    for (e = (const u16 FAR *)t; (u32)FP_OFF(e) + 7 <= lsl(FP_SEG(e)) && e[0]; e += 4)
        if ((e[0] | 7) == (cs | 7) && ip >= e[1] && ip < e[2])
            return e[3];
    return 0;
}

/* KERNEL's own "__GP" (KERNEL.342): empty. 3.1's KERNEL lets its
   string and validation functions fault and recovers through its table
   (35 entries); w16's check pointers before using them (kstr.asm,
   kmisc.c), so it has no ranges to list. */
u16 gp_table[4];
#pragma aux gp_table "__GP"

/* KERNEL.338 */
u16 KAPI HasGPHandler(u32 addr)
{
    return gp_find((u16)(addr >> 16), (u16)addr);
}

/* readable and present (VERR passes a not-present one, whose load
   faults again: KERNEL audit, medium 20) */
static u16 seg_ok(u16 sel);
#pragma aux seg_ok = \
    "xor ax, ax" \
    "verr dx" \
    "jnz no" \
    "lar ax, dx" \
    "and ax, 8000h" \
    "no:" \
    parm [dx] value [ax] modify exact [ax];

/* called by fault_exit (kentry.asm): the recovery offset for a GP,
   stack or not-present fault in a "__GP" range, else 0 */
u16 __cdecl __far gp_handler(void)
{
    u16 *f = fault_regs, r;
    if (f[0] != 0x0B && f[0] != 0x0C && f[0] != 0x0D)
        return 0;
    r = gp_find(f[3], f[2]);
    if (r) {
        if (f[14] && !seg_ok(f[14]))
            f[14] = 0;
        if (f[15] && !seg_ok(f[15]))
            f[15] = 0;
    }
    return r;
}

/* ---- reading memory the report does not own ----
   The fault report reads the faulting stack, the module list and the
   names in it: any of them can be what went wrong. Every read is checked
   first: the selector must be readable and present (VERR, LAR), not an
   expand-down segment (whose valid offsets lie above its limit), and the
   bytes inside the limit (LSL). A read that does not pass is not made;
   the raw value is printed instead. */
static int readable(u16 sel, u16 off, u16 len)
{
    u16 ar;
    if (!len)
        return 1;
    if ((u32)off + len - 1 > 0xFFFFUL || !seg_ok(sel))
        return 0;
    ar = lar_any(sel);
    if (!(ar & 0x0800) && (ar & 0x0400))        /* expand-down data */
        return 0;
    return (u32)off + len - 1 <= (u32)lsl(sel);
}

/* " (MODULE:n base ... limit ...)" for the module segment sel belongs
   to; where() writes to the screen in a report, to kdbg in a note */
static int where_dbg;
static void wputs(const char FAR *s) { if (where_dbg) kdbg(s); else kputs(s); }
static void wputhex(u16 v) { if (where_dbg) kdbghex(v); else kputhex(v); }
static void wputdec(u16 v)
{
    char b[6];
    int i = 5;
    b[5] = 0;
    do {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    wputs(b + i);
}

/* a module's name, copied out (at most MODNAME_MAX characters, even
   when the module's block was overwritten) */
static void wputname(module FAR *m)
{
    char n[MODNAME_MAX + 1];
    u16 i;
    for (i = 0; i < MODNAME_MAX && m->name[i]; i++)
        n[i] = (m->name[i] >= ' ' && m->name[i] < 0x7F) ? m->name[i] : '?';
    n[i] = 0;
    wputs(n);
}

/* the next module of the list, or 0 when it cannot be read */
#define MOD_HEAD_LEN ((u16)(u32)(void FAR *)&((module FAR *)0)->seg)
static module FAR *mod_ok(module FAR *m)
{
    if (!m || !readable(FP_SEG(m), FP_OFF(m), MOD_HEAD_LEN))
        return 0;
    if (m->seg_count && !readable(FP_SEG(m), FP_OFF(m) + MOD_HEAD_LEN,
                                  m->seg_count * (u16)sizeof(modseg)))
        return 0;
    return m;
}

static void wbase(u16 sel)
{
    u32 b = dpmi_get_base(sel);
    wputs("base ");
    wputhex((u16)(b >> 16));
    wputhex((u16)b);
    wputs(" limit ");
    wputhex(lsl(sel));
}

void where(u16 sel)
{
    module FAR *m;
    u16 i, n;
    for (m = mod_ok(module_first), n = 0; m && n < 1024; m = mod_ok(m->next), n++)
        for (i = 0; i < m->seg_count; i++)
            if (m->seg[i].sel && (m->seg[i].sel | 7) == (sel | 7)) {
                wputs(" (");
                wputname(m);
                wputs(":");
                wputdec(i + 1);
                wputs(" ");
                wbase(sel);
                wputs(")");
                return;
            }
    for (m = mod_ok(module_first), n = 0; m && n < 1024; m = mod_ok(m->next), n++)
        if ((m->ne_sel | 7) == (sel | 7)) {
            wputs(" (module table of ");
            wputname(m);
            wputs(")");
            return;
        }
    if (sel) {
        wputs(" (");
        wbase(sel);
        wputs(")");
    }
}

static void reg(const char *name, u16 v)
{
    kputs(" ");
    kputs(name);
    kputs("=");
    kputhex(v);
}

static u16 ring[I21_N * 5];

extern u16 __cdecl wd_ticks, wd_limit, wd_pos;
extern u16 __cdecl wd_samples[128];
extern void __far wd_int70(void);
#pragma aux wd_int70 "*"

static void print_int21_ring(u16 pos, u16 n);

/* ---- the report's steps ----
   The report runs on a stack of its own (kentry.asm, fault_exit: never
   the faulting one, which may be the fault), with DS and ES KERNEL's.
   It must not fault; should it fault all the same, kentry.asm comes back
   to fault_report_again on a fresh stack, which notes the step that
   faulted, leaves it out from then on and prints the report again from
   the start. A user's report (Visual C++ 1.52's WPS and OLE2VIEW on w16)
   showed "w16: exc" and Windows gone: the report itself had died. */
#define RS_PRINT        0x0000          /* plain printing: never left out */
#define RS_WHERE        0x0001          /* what a selector is */
#define RS_STACK        0x0002          /* the words at SS:SP */
#define RS_FRAMES       0x0004          /* the BP frames */
#define RS_RING         0x0008          /* the last INT 21h calls */
#define RS_TRACE        0x0010          /* [w16] trace */
#define RS_QUIET        0x0020          /* the interrupt vectors, INT 21h */
#define RS_OEM          0x0040          /* USER's DisableOEMLayer */
#define RS_TEXT         0x0080          /* INT 10h text mode, 50 rows */
#define RS_FILE         0x0100          /* the crash log's DOS calls */

extern u8 __cdecl rpt_active;           /* kentry.asm: a report is running */
static u16 rpt_regs[16];                /* the fault being reported */
static u16 rpt_nest[16];                /* the latest fault in the report */
static u16 rpt_pos;                     /* i21_pos at the fault */
static u16 rpt_step;                    /* the step running */
static u16 rpt_skip;                    /* steps that faulted: left out */
static u16 rpt_done;                    /* steps done once (RS_OEM) */
static u16 rpt_count;                   /* faults in the report */
static u16 rpt_test;                    /* [w16] faultreptest: steps that
                                           fault on purpose, once each */
static u32 rpt_host21;                  /* the host's INT 21h vector */
static u32 rpt_irq[16];                 /* the IRQ vectors at the boot */
static u16 lim_words, lim_frames;       /* the pass's stack words, frames */

/* ---- the crash log ----
   The whole report goes to a file as well, after the screen has it:
   [w16] CrashLog (default 1; 0: none) puts it in the Windows directory,
   W16CRASH.TXT. Each report is appended, after a separator line, with
   a header (DOS's date and time, w16's version and build); a file over
   64 KB at the start of a report is emptied first. The log gets every
   line the screen does and what the screen leaves out for room: 64
   stack words, 32 frames, the 64 INT 21h calls of the ring.
   The report is printed twice, the same steps each time: first to the
   screen, then (the log's pass) through kputs_hook into the file. The
   screen goes first: its pass is the one a fault inside the report
   restarts (below what it printed, up to three times), and a file step
   before it would spend those restarts, or hang in DOS, before anything
   was on the screen. A fault in the log's pass never goes back to the
   screen's: it is noted in the log, the step that faulted is left out
   and the log gets the report again (at most three times), then the
   report ends. Every DOS call of the log's is the step RS_FILE (after a
   fault in one the file is left alone); one that fails (the disk full,
   the file read-only, INT 21h refusing) ends the log's writing
   silently. "w16: report written to ..." is printed only when the
   whole log was written. */
static char rpt_path[DOS_PATHMAX];      /* the log; "" with CrashLog=0 */
static u16 rpt_fh;                      /* its handle while open */
static u8 rpt_fopen;                    /* open */
static u8 rpt_file;                     /* the log's pass is running */
static u8 rpt_logged;                   /* it has run (once only) */
static u8 rpt_fail;                     /* a call failed: no more */
static u8 rpt_written;                  /* the whole log was written */
static u16 rpt_fcount;                  /* faults in the log's pass */
static char rpt_buf[512];               /* the log's output, buffered */
static u16 rpt_bn;

/* the screen's pass: kputs to standard output, the rows counted (the
   BIOS wraps a line at its 80th character) */
static u16 scr_row, scr_col;
static u8 scr_known;

static void __far scr_put(const char FAR *s)
{
    u16 i;
    kputs_out(s);
    for (i = 0; s[i]; i++)
        if (s[i] == '\n')
            scr_row++;
        else if (s[i] == '\r')
            scr_col = 0;
        else if (++scr_col == 80) {
            scr_row++;
            scr_col = 0;
        }
}

static u16 pm_vec_get(u8 n, u16 *off)
{
    regs16 r;
    kmemset(&r, 0, sizeof r);
    r.ax = 0x0204;
    r.bx = n;
    dpmi31(&r);
    *off = r.dx;
    return r.cx;
}

static void pm_vec_set(u8 n, u32 v)
{
    regs16 r;
    kmemset(&r, 0, sizeof r);
    r.ax = 0x0205;
    r.bx = n;
    r.cx = (u16)(v >> 16);
    r.dx = (u16)v;
    dpmi31(&r);
}

static u8 irq_vec(u16 i)
{
    return (u8)(i < 8 ? 0x08 + i : 0x70 + i - 8);
}

/* (called from fault_init: before any driver hooks an interrupt) */
static void rpt_init(void)
{
    u16 i, off, sel;
    for (i = 0; i < 16; i++) {
        sel = pm_vec_get(irq_vec(i), &off);
        rpt_irq[i] = ((u32)sel << 16) | off;
    }
    rpt_host21 = old21;
    rpt_test = GetPrivateProfileInt("w16", "faultreptest", 0, sysini);
    /* the crash log's path, made now: nothing of Windows is asked once
       it has faulted */
    rpt_path[0] = 0;
    if (GetPrivateProfileInt("w16", "CrashLog", 1, sysini)) {
        static const char name[] = "W16CRASH.TXT";
        u16 n = GetWindowsDirectory(rpt_path, sizeof rpt_path);
        if (n && n < sizeof rpt_path - sizeof name - 1) {
            if (rpt_path[n - 1] != '\\')
                rpt_path[n++] = '\\';
            kmemcpy(rpt_path + n, name, sizeof name);
        } else
            rpt_path[0] = 0;
    }
}

/* a general protection fault: DS loaded with a selector past the LDT's
   end (DOSBox's processor does not fault on a null selector's use) */
static void gp_now(void);
#pragma aux gp_now = "push ds" "mov ax, 0FFFFh" "mov ds, ax" "pop ds" modify [ax];

/* enter a step: 0 when it is to be left out. [w16] faultreptest=MASK
   (w16's own, for tests: the boot-test case faultrep) makes the steps in
   MASK fault once, on purpose. */
static u16 step(u16 s)
{
    if (rpt_skip & s)
        return 0;
    rpt_step = s;
    if (rpt_test & s) {
        rpt_test &= ~s;
        gp_now();
    }
    return 1;
}

static void step_end(void)
{
    rpt_step = RS_PRINT;
}

/* where(), as a step */
static void wh(u16 sel)
{
    if (step(RS_WHERE))
        where(sel);
    step_end();
}

/* The faulting stack: 16 words from SS:SP, then the chain of BP frames
   (a far frame's BP is odd: Windows' prologue), each with the return
   address it holds, as far as the words can be read. */
static void print_stack(u16 ss, u16 sp, u16 bp)
{
    u16 i, w[6];
    if (step(RS_STACK)) {
        kputs("stack:");
        for (i = 0; i < 16 && (u32)sp + 2 * i < 0xFFFFUL && readable(ss, sp + 2 * i, 2); i++) {
            kputs(" ");
            kputhex(*(u16 __far *)MK_FP(ss, sp + 2 * i));
        }
        if (!i)
            kputs(" (not readable)");
        kputs("\r\n");
        /* (the crash log: more words, 16 a line, "stack+0020:" for the
           words from SP+20h) */
        for (; i >= 16 && i < lim_words && (u32)sp + 2 * i < 0xFFFFUL &&
               readable(ss, sp + 2 * i, 2); i++) {
            if (i % 16 == 0) {
                kputs("stack+");
                kputhex(2 * i);
                kputs(":");
            }
            kputs(" ");
            kputhex(*(u16 __far *)MK_FP(ss, sp + 2 * i));
            if (i % 16 == 15)
                kputs("\r\n");
        }
        if (i > 16 && i % 16)
            kputs("\r\n");
    }
    step_end();
    if (!step(RS_FRAMES)) {
        step_end();
        return;
    }
    for (i = 0; i < lim_frames && bp && readable(ss, bp & ~1, 12); i++) {
        kmemcpy(w, MK_FP(ss, bp & ~1), sizeof w);
        rpt_step = RS_PRINT;
        kputs(" frame ");
        kputhex(bp);
        kputs(" ret ");
        if (bp & 1) {
            kputhex(w[2]);
            kputs(":");
            kputhex(w[1]);
            wh(w[2]);
        } else {
            kputhex(w[1]);
        }
        kputs(" args ");
        kputhex(w[3]);
        kputs(" ");
        kputhex(w[4]);
        kputs(" ");
        kputhex(w[5]);
        kputs("\r\n");
        if ((w[0] & ~1) <= (bp & ~1))
            break;
        bp = w[0];
        if (!step(RS_FRAMES))
            break;
    }
    step_end();
}

/* The interrupts quiet: the hardware interrupts' protected-mode vectors
   as they were before the drivers came (they reach the BIOS again, not
   handlers of a Windows that is going), and INT 21h straight to the host
   (no hook of a program or of KERNEL between the report and DOS) */
static void quiet(void)
{
    u16 i;
    if (step(RS_QUIET)) {
        for (i = 0; i < 16; i++)
            if (rpt_irq[i])
                pm_vec_set(irq_vec(i), rpt_irq[i]);
        if (rpt_host21)
            pm_vec_set(0x21, rpt_host21);
    }
    step_end();
}

/* Back to text mode before the report: USER's DisableOEMLayer, as
   Windows' own KERNEL does before a fatal message (Pietrek, TextMode):
   the cursor off, the mouse, keyboard and system timers disabled (the
   mouse's BIOS packet handler removed: left in place, it pointed into a
   Windows that was gone, and DOS sat in a loop of invalid opcodes once
   the mouse moved), the display driver's Disable (BIOS mode 3). Then
   INT 10h AX=0003h in any case (text_mode). Each once only. */
static void oem_off(void)
{
    module FAR *u;
    u32 p;
    if (!(rpt_done & RS_OEM) && step(RS_OEM)) {
        rpt_done |= RS_OEM;
        u = module_find("USER");
        p = u ? GetProcAddress(u->ne_sel, "#4") : 0;   /* DisableOEMLayer */
        if (p)
            ((void (__far __pascal *)(void))p)();
    }
    step_end();
    /* (once too: a report printed again after a fault in it goes on
       below what the first attempt printed, never over it) */
    if (!(rpt_done & RS_TEXT) && step(RS_TEXT)) {
        rpt_done |= RS_TEXT;
        if (text_mode()) {
            rows50();
            scr_row = scr_col = 0;
            scr_known = 1;
        }
    }
    step_end();
    win_screen = 0;
    /* not set here (a fault before USER's screen was up, or a text mode
       that faulted): the rows as they are, from where the cursor is */
    if (!scr_known) {
        scr_known = 1;
        rows_now();
        scr_row = rm_int10(0x0300, 0, 0) ? (u8)(rmc.edx >> 8) : 0;
    }
}

static void report_body(void);
static void report_lines(void);
static void report_end(void);
static void fault_note(void);
static void log_close(void);
static void log_end(void);
static void __far log_put(const char FAR *s);

/* kentry.asm, fault_exit: a fault; reported on the report's stack */
void __cdecl fault_report(void)
{
    kmemcpy(rpt_regs, fault_regs, sizeof rpt_regs);
    rpt_pos = i21_pos;
    kmemcpy(ring, i21_ring, sizeof ring);
    report_body();
}

/* the last resort: the registers as they are, nothing looked up */
static void raw_line(const char *what, const u16 *f)
{
    kputs(what);
    kputhex(f[0]);
    kputs(" at ");
    kputhex(f[3]);
    kputs(":");
    kputhex(f[2]);
    reg("err", f[1]);
    reg("AX", f[7]);
    reg("BX", f[8]);
    reg("CX", f[9]);
    reg("DX", f[10]);
    reg("SI", f[11]);
    reg("DI", f[12]);
    reg("BP", f[13]);
    reg("FL", f[4]);
    reg("DS", f[14]);
    reg("ES", f[15]);
    reg("SS", f[6]);
    reg("SP", f[5]);
    kputs("\r\n");
}

/* kentry.asm, fault_exit: a fault while a report was running */
void __cdecl fault_report_again(void)
{
    regs16 r;
    kmemcpy(rpt_nest, fault_regs, sizeof rpt_nest);
    rpt_skip |= rpt_step;
    rpt_step = RS_PRINT;
    if (rpt_file) {
        /* in the crash log's pass: the screen has the report already */
        if (++rpt_fcount <= 3 && rpt_fopen && !(rpt_skip & RS_FILE)) {
            kputs_hook = log_put;
            fault_note();
            report_lines();
        }
        if (rpt_fcount <= 6)
            log_close();
        log_end();
        report_end();
    }
    kputs_hook = 0;
    if (++rpt_count <= 3) {
        report_body();
        return;
    }
    if (rpt_count == 4) {
        /* printing itself faults: the raw values once more, then out */
        rpt_skip = 0xFFFF;
        raw_line("w16: exception ", rpt_regs);
        raw_line("w16: and in the report: exception ", rpt_nest);
        kexit_to_dos(0xFF);
    }
    /* not even that: out without a word */
    if (rpt_host21)
        pm_vec_set(0x21, rpt_host21);
    kmemset(&r, 0, sizeof r);
    r.ax = 0x4CFF;
    dos21(&r);
}

/* what went wrong in the report, raw (on a line of its own: the fault
   may have come in the middle of one) */
static void fault_note(void)
{
    kputs("\r\n");
    raw_line("w16: a fault in the fault report: exception ", rpt_nest);
    kputs("w16: the report again, without the step that faulted\r\n");
}

/* The screen's INT 21h calls: as many as the rows left hold, the last
   ROWS_AFTER kept for what follows (the log's line, "back to DOS", DOS's
   prompt); at least 8, or 16 (the ring's old size) without a crash log
   to hold the rest. The log gets all 64. */
#define ROWS_AFTER 5
static u16 ring_count(void)
{
    u16 n = 0, lo = rpt_path[0] ? 8 : 16;
    if (rpt_file)
        return I21_N;
    if (rpt_rows > scr_row + 1 + ROWS_AFTER)
        n = rpt_rows - scr_row - 1 - ROWS_AFTER;
    return n < lo ? lo : n > I21_N ? I21_N : n;
}

static void report_lines(void)
{
    u16 *f = rpt_regs;
    if (rpt_count)
        fault_note();
    kputs("w16: exception ");
    kputhex(f[0]);
    kputs(" at ");
    kputhex(f[3]);
    kputs(":");
    kputhex(f[2]);
    wh(f[3]);
    reg("err", f[1]);
    if (f[1] & 0xFFF8) {
        /* the selector the error code names (its RPL is not in the code):
           its access rights and what it is (QuickTime's movie player:
           MMSYSTEM loading SS with an even global handle) */
        reg("ar", lar_any(f[1] | 7));
        wh(f[1] | 7);
    }
    kputs("\r\n");
    reg("AX", f[7]);
    reg("BX", f[8]);
    reg("CX", f[9]);
    reg("DX", f[10]);
    reg("SI", f[11]);
    reg("DI", f[12]);
    reg("BP", f[13]);
    reg("FL", f[4]);
    kputs("\r\n");
    reg("DS", f[14]);
    wh(f[14]);
    reg("ES", f[15]);
    wh(f[15]);
    reg("SS", f[6]);
    wh(f[6]);
    reg("SP", f[5]);
    kputs("\r\n");
    print_stack(f[6], f[5], f[13]);
    if (step(RS_RING))
        print_int21_ring(rpt_pos, ring_count());
    step_end();
    if (step(RS_TRACE))
        trace_print();
    step_end();
}

/* ---- the crash log's DOS calls ---- */

/* one INT 21h call of the log's, as the step RS_FILE: 1 when it was made
   and succeeded */
static int log_call(regs16 *r)
{
    u16 was = rpt_step;
    int ok = 0;
    if (!rpt_fail && step(RS_FILE))
        ok = !dos21(r);
    rpt_step = was;
    return ok;
}

static void log_flush(void)
{
    regs16 r;
    u16 n = rpt_bn;
    rpt_bn = 0;
    if (!n || !rpt_fopen || rpt_fail)
        return;
    kmemset(&r, 0, sizeof r);
    r.ax = 0x4000;
    r.bx = rpt_fh;
    r.cx = n;
    r.dx = FP_OFF((char FAR *)rpt_buf);
    r.ds = FP_SEG((char FAR *)rpt_buf);
    if (!log_call(&r) || r.ax != n)
        rpt_fail = 1;                   /* (the disk full) */
}

/* kputs_hook in the log's pass */
static void __far log_put(const char FAR *s)
{
    while (*s) {
        if (rpt_bn == sizeof rpt_buf)
            log_flush();
        rpt_buf[rpt_bn++] = *s++;
    }
}

static void log_open(void)
{
    regs16 r;
    kmemset(&r, 0, sizeof r);
    r.ax = 0x3D01;                      /* open for writing */
    r.dx = FP_OFF((char FAR *)rpt_path);
    r.ds = FP_SEG((char FAR *)rpt_path);
    if (log_call(&r)) {
        rpt_fh = r.ax;
        rpt_fopen = 1;
        kmemset(&r, 0, sizeof r);
        r.ax = 0x4202;                  /* to the end: DX:AX its size */
        r.bx = rpt_fh;
        if (!log_call(&r)) {
            rpt_fail = 1;
            return;
        }
        if (r.dx == 0 && r.ax == 0)
            return;
        if (r.dx == 0 || (r.dx == 1 && r.ax == 0)) {
            log_put("\r\n========================================"
                    "=======================================\r\n");
            return;
        }
        /* over 64 KB: emptied (made again) */
        kmemset(&r, 0, sizeof r);
        r.ax = 0x3E00;
        r.bx = rpt_fh;
        rpt_fopen = 0;
        log_call(&r);
    }
    kmemset(&r, 0, sizeof r);
    r.ax = 0x3C00;                      /* made (or emptied) */
    r.dx = FP_OFF((char FAR *)rpt_path);
    r.ds = FP_SEG((char FAR *)rpt_path);
    if (!log_call(&r))
        return;
    rpt_fh = r.ax;
    rpt_fopen = 1;
}

static void log_close(void)
{
    regs16 r;
    log_flush();
    if (!rpt_fopen)
        return;
    rpt_fopen = 0;
    kmemset(&r, 0, sizeof r);
    r.ax = 0x3E00;
    r.bx = rpt_fh;
    if (log_call(&r) && !rpt_fail)
        rpt_written = 1;
}

static void put2(u16 v)
{
    char b[3];
    b[0] = (char)('0' + v / 10 % 10);
    b[1] = (char)('0' + v % 10);
    b[2] = 0;
    kputs(b);
}

/* "w16 crash report, 1992-06-15 10:00:05, w16 0.2 (abc1234), KRNL386" */
static void log_header(void)
{
    regs16 r;
    kputs("w16 crash report, ");
    kmemset(&r, 0, sizeof r);
    r.ax = 0x2A00;                      /* the date: CX year, DH month, DL day */
    if (log_call(&r)) {
        kputdec(r.cx);
        kputs("-");
        put2(r.dx >> 8);
        kputs("-");
        put2(r.dx & 0xFF);
        kmemset(&r, 0, sizeof r);
        r.ax = 0x2C00;                  /* the time: CH:CL:DH */
        if (log_call(&r)) {
            kputs(" ");
            put2(r.cx >> 8);
            kputs(":");
            put2(r.cx & 0xFF);
            kputs(":");
            put2(r.dx >> 8);
        }
    } else
        kputs("(no date)");
    kputs(", w16 " W16_VERSION " (" W16_BUILD "), ");
#ifdef KRNL386
    kputs("KRNL386\r\n");
#else
    kputs("KRNL286\r\n");
#endif
}

/* the log's pass over: back to the screen */
static void log_end(void)
{
    kputs_hook = 0;
    rpt_file = 0;
    kernel_psp(0);
}

/* the crash log's pass (once) */
static void crash_log(void)
{
    if (!rpt_path[0] || rpt_logged)
        return;
    rpt_logged = 1;
    rpt_file = 1;
    lim_words = 64;
    lim_frames = 32;
    kernel_psp(1);
    log_open();
    if (rpt_fopen) {
        kputs_hook = log_put;
        log_header();
        report_lines();
        log_close();
    }
    log_end();
}

static void report_end(void)
{
    kputs_hook = 0;
    if (rpt_written) {
        kputs("w16: report written to ");
        kputs(rpt_path);
        kputs("\r\n");
    }
    kexit_to_dos(0xFF);
}

static void report_body(void)
{
    quiet();
    oem_off();
    kputs_hook = scr_put;
    lim_words = 16;
    lim_frames = 12;
    report_lines();
    kputs_hook = 0;
    crash_log();
    report_end();
}

static void print_int21_ring(u16 pos, u16 n)
{
    u16 i, k;
    if (n > I21_N)
        n = I21_N;
    kputs("last INT 21h calls (oldest first):\r\n");
    for (i = n; i--; ) {
        /* (the entry i before the latest; pos is the latest's byte) */
        k = (pos / 10 + I21_N - i) % I21_N * 5;
        if (!ring[k + 4])
            continue;
        reg("AX", ring[k]);
        reg("BX", ring[k + 1]);
        reg("DX", ring[k + 2]);
        kputs(" from ");
        kputhex(ring[k + 4]);
        kputs(":");
        kputhex(ring[k + 3]);
        where(ring[k + 4]);
        kputs("\r\n");
    }
}

/* The boot watchdog: SYSTEM.INI [w16] watchdog=SECONDS (w16's own key;
   0 or none: off). While Windows runs, SECONDS without progress (a boot
   log line, a module loaded) make KERNEL report where the processor has
   been (timer samples) and leave to DOS. */
static u8 inb(u16 port);
#pragma aux inb = "in al, dx" parm [dx] value [al];
static void outb(u16 port, u8 v);
#pragma aux outb = "out dx, al" parm [dx] [al];

/* (the RTC's registers A and B and the protected-mode INT 70h vector
   as they were before the watchdog: wd_stop puts them back, so the RTC
   does not go on interrupting 16 times a second after it; each CMOS
   read-modify-write with interrupts off, as the RTC's index register is
   shared: KERNEL audit, L14) */
static u8 wd_on, wd_rtc_a, wd_rtc_b;
static u16 wd_old_cs, wd_old_ip;

static u16 irq_off(void);
#pragma aux irq_off = "pushf" "cli" "pop ax" value [ax];
static void irq_restore(u16 f);
/* (IF explicitly: in 386 Enhanced mode POPF leaves it alone, 12.14) */
#pragma aux irq_restore = "push ax" "test ah, 2" "jz L1" "sti" "L1:" "popf" parm [ax];

static u8 cmos_get(u8 reg)
{
    u16 f = irq_off();
    u8 v;
    outb(0x70, reg);
    v = inb(0x71);
    irq_restore(f);
    return v;
}

static void cmos_set(u8 reg, u8 v)
{
    u16 f = irq_off();
    outb(0x70, reg);
    outb(0x71, v);
    irq_restore(f);
}

/* the longest stretch without progress since wd_start, in RTC ticks
   (16 a second), and the code segment a timer sample half a second
   before its end was in: printed when Windows ends ([w16] exitmsg), so that a
   test can see how close a busy program came to its hangwatch limit */
static u16 wd_max, wd_max_cs;
static u8 wd_measure;

void wd_start(u16 seconds)
{
    regs16 r;
    u16 f;
    if (!seconds)
        return;
    wd_ticks = 0;
    wd_max = 0;
    wd_measure = 1;
    wd_limit = seconds < 4096 ? seconds * 16 : 0xFFFF;   /* (audit, L14) */
    if (wd_on)
        return;
    r.ax = 0x0204;
    r.bx = 0x70;
    r.ds = r.es = 0;
    if (dpmi31(&r))
        return;
    wd_old_cs = r.cx;
    wd_old_ip = r.dx;
    r.ax = 0x0205;
    r.bx = 0x70;
    r.cx = FP_SEG(wd_int70);
    r.dx = FP_OFF(wd_int70);
    r.ds = r.es = 0;
    if (dpmi31(&r))
        return;
    wd_on = 1;
    /* RTC: 16 periodic interrupts a second (rate 12), enabled; IRQ 8 and
       the cascade unmasked */
    wd_rtc_a = cmos_get(0x0A);
    wd_rtc_b = cmos_get(0x0B);
    cmos_set(0x0A, (u8)((wd_rtc_a & 0xF0) | 0x0C));
    cmos_set(0x0B, (u8)(wd_rtc_b | 0x40));
    cmos_get(0x0C);
    f = irq_off();
    outb(0xA1, (u8)(inb(0xA1) & ~0x01));
    outb(0x21, (u8)(inb(0x21) & ~0x04));
    irq_restore(f);
}

void wd_progress(void)
{
    u16 t = wd_ticks;
    if (t > wd_max) {
        wd_max = t;
        /* half a second before the end: inside the stretch, not in the
           KERNEL code that ends it */
        wd_max_cs = wd_samples[((wd_pos - 32) & 255) / 2 + 1];
    }
    wd_ticks = 0;
}

/* [w16] exitmsg with a watchdog on: the longest stretch without progress
   (for hangwatch: without a task switch or a pass of the idle loop) */
void wd_summary(void)
{
    if (!wd_measure || !wd_on)
        return;
    kputs("w16: longest stretch without progress ");
    kputdec(wd_max / 16);
    kputs(".");
    kputdec((u16)((wd_max % 16) * 10 / 16));
    /* (a sample taken in real mode, in DOS or the BIOS, is a paragraph,
       not a selector: no module) */
    kputs(" s; half a second before its end CS=");
    kputhex(wd_max_cs);
    where(wd_max_cs);
    kputs("\r\n");
}

void wd_stop(void)
{
    regs16 r;
    wd_limit = 0;
    wd_measure = 0;
    if (!wd_on)
        return;
    wd_on = 0;
    cmos_set(0x0B, (u8)((cmos_get(0x0B) & ~0x40) | (wd_rtc_b & 0x40)));
    cmos_set(0x0A, (u8)((cmos_get(0x0A) & 0xF0) | (wd_rtc_a & 0x0F)));
    cmos_get(0x0C);
    r.ax = 0x0205;
    r.bx = 0x70;
    r.cx = wd_old_cs;
    r.dx = wd_old_ip;
    r.ds = r.es = 0;
    dpmi31(&r);
}

void __cdecl wd_report(void)
{
    static u16 s[128];
    u16 i, j, pos = wd_pos, ipos = i21_pos;
    kmemcpy(ring, i21_ring, sizeof ring);
    kmemcpy(s, wd_samples, sizeof s);
    text_mode();
    kputs("w16: no boot progress; timer samples (CS:IP, most recent last):\r\n");
    for (i = 0; i < 64; i++) {
        j = ((pos + 4 + i * 4) & 255) / 2;
        if (!s[j + 1])
            continue;
        kputhex(s[j + 1]);
        kputs(":");
        kputhex(s[j]);
        where(s[j + 1]);
        kputs("\r\n");
    }
    print_int21_ring(ipos, 16);
    trace_print();
    kexit_to_dos(0xFE);
}

/* K327 (kentry.asm): note the first parameter errors on standard output,
   with the caller's frame (SS:BP, BP even) and its first words above, the
   API's arguments when the caller is a validation layer's entry, each
   that names a selector with its base and limit */
static u16 get_ss(void);
#pragma aux get_ss = "mov ax, ss" value [ax] modify exact [ax];

void __cdecl k327_note(u16 err, u16 ip, u16 cs, u16 bp)
{
    static u16 count;
    u16 i, w;
    if (++count > 8)
        return;
    kdbg("w16: parameter error ");
    kdbghex(err);
    kdbg(" at ");
    kdbghex(cs);
    kdbg(":");
    kdbghex(ip);
    where_dbg = 1;
    where(cs);
    kdbg("\r\n  frame ");
    kdbghex(bp);
    bp &= ~1u;
    for (i = 0; i < 12 && (u32)bp + 2 * i + 1 <= lsl(get_ss()); i++) {
        w = *(const u16 FAR *)MK_FP(get_ss(), bp + 2 * i);
        kdbg(" ");
        kdbghex(w);
    }
    kdbg("\r\n");
    for (i = 2; i < 12 && (u32)bp + 2 * i + 1 <= lsl(get_ss()); i++) {
        w = *(const u16 FAR *)MK_FP(get_ss(), bp + 2 * i);
        if ((w & 4) && seg_ok(w)) {
            u32 h = GlobalHandle(w);
            kdbg("  ");
            kdbghex(w);
            where(w);
            if ((u16)h) {
                kdbg(" handle ");
                kdbghex((u16)h);
                kdbg(" size ");
                kdbghex((u16)(GlobalSize((u16)h) >> 16));
                kdbghex((u16)GlobalSize((u16)h));
            }
            kdbg("\r\n");
        }
    }
    where_dbg = 0;
}

/* INT 21h AH=4Ch with no current task (kentry.asm): the KERNEL process
   itself ends. While booting Windows, say from where (the stack holds
   AX, DS, then the INT's IRET frame: IP, CS, FLAGS). */
void __cdecl dos_exit_note(u16 FAR *sp)
{
    if (!winboot_active)
        return;
    kputs("w16: INT 21h AH=4Ch with no task, from ");
    kputhex(sp[3]);
    kputs(":");
    kputhex(sp[2]);
    where(sp[3]);
    kputs(" AX=");
    kputhex(sp[0]);
    kputs("\r\n");
    trace_print();
}

/* [w16] int21log=1 (kentry.asm i21_logcall): one debuglog line per INT 21h
   call that goes on to DOS: the calling task's module, CS:IP, the
   registers before (AX BX CX DX SI DI DS ES) and after (AX BX CX DX, C
   for the carry), and the path of the calls that take one. A debugging
   aid: what a program asks of DOS. */
extern u16 __cdecl i21_log;
u16 KAPI_ES IsBadReadPtr(void FAR *p, u16 n);
void dos_psp_note(u16 *real, u16 *task);         /* task.c */
void dos_psp_restore(u16 seg);

static void i21_hex(char *b, u16 *n, u16 v)
{
    static const char hex[] = "0123456789ABCDEF";
    u16 i;
    for (i = 0; i < 4; i++)
        b[(*n)++] = hex[(v >> (12 - 4 * i)) & 15];
}

void __cdecl __far i21_note(u16 FAR *in, u16 FAR *out)
{
    static const u8 path_calls[] = { 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x41,
                                     0x43, 0x4B, 0x4E, 0x56, 0x5A, 0x5B,
                                     0x6C };
    char b[200];
    u16 n = 0, i, k, real, task;
    u16 ah = in[0] >> 8;
    const char FAR *s = 0;
    module FAR *m;
    if (!i21_log)
        return;
    /* in: AX BX CX DX SI DI DS ES, then the saved BP and the caller's
       IRET frame (IP CS FLAGS); out: AX BX CX DX SI DI DS ES FLAGS */
    m = cur_task ? module_of(*(u16 FAR *)MK_FP(cur_task, 0x1E)) : 0;
    for (i = 0; m && m->name[i] && i < 8; i++)
        b[n++] = m->name[i];
    b[n++] = ' ';
    i21_hex(b, &n, in[10]);
    b[n++] = ':';
    i21_hex(b, &n, in[9]);
    for (k = 0; k < 8; k++) {
        b[n++] = ' ';
        i21_hex(b, &n, in[k]);
    }
    b[n++] = ' ';
    b[n++] = '>';
    for (k = 0; k < 4; k++) {
        b[n++] = ' ';
        i21_hex(b, &n, out[k]);
    }
    b[n++] = (out[8] & 1) ? 'C' : ' ';
    dos_psp_note(&real, &task);
    {
        b[n++] = ' ';
        b[n++] = 'P';
        i21_hex(b, &n, real);
        if (real != task) {
            b[n++] = '!';
            i21_hex(b, &n, task);
        }
    }
    for (i = 0; i < sizeof path_calls; i++)
        if (ah == path_calls[i])
            s = (const char FAR *)MK_FP(in[6], ah == 0x6C ? in[4] : in[3]);
    if (s && !IsBadReadPtr((void FAR *)s, 1)) {
        b[n++] = ' ';
        b[n++] = '"';
        for (i = 0; i < 80 && !IsBadReadPtr((void FAR *)(s + i), 1) && s[i]; i++)
            b[n++] = s[i];
        b[n++] = '"';
    }
    b[n++] = '\r';
    b[n++] = '\n';
    b[n] = 0;
    kdbg_file(b);
    dos_psp_restore(real);
}
