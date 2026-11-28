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
u32 filecdr_proc;               /* FileCdr's callback (kentry.asm tests it) */
static u16 filecdr_task;        /* the task that installed it */

/* ExitKernel (KERNEL.2): leave Windows, back to DOS. USER calls it at the
   end of ExitWindows with one word, the exit code (Pietrek, ch. 1:
   "ExitKernel( ExitCode)"), and it pops that word: should it ever return,
   the caller's stack stays right (it popped none). KERNEL ends with INT
   21h AH=4Ch, AL = that code (Pietrek, ch. 1, ExitKernel: "AH = 4CH,
   AX = exitCode"), which WIN.COM reads: 42h (EW_RESTARTWINDOWS) starts
   Windows again, 44h (USER's ExitWindowsExec, after it put the program
   in WIN.COM's buffer, INT 2Fh 4B20h) runs that program first (ch. 1,
   "the Big Bang"). Observed: ExitWindows(0) ends WIN with ERRORLEVEL 0
   (TK55); Win32s 1.30c's Setup restarts Windows through ExitWindowsExec,
   on 3.1 and now on w16's KERNEL, which always exited with 0 before, so
   that Windows never came back and _MSSETUP.BAT never ran. Not done:
   43h (EW_REBOOTSYSTEM), for which 3.1 calls the Reboot device or INT
   19h; w16 exits with 43h. */
void KAPI ExitKernel(u16 code)
{
    win_screen = 0;                     /* USER's ExitWindows disabled the
                                           display: text mode */
    trace_print();                      /* [w16] trace, if any */
    kexit_to_dos((u8)code);
}

/* LockCurrentTask (KERNEL.33): Schulman: lock (nonzero) or unlock the
   current task; IsTaskLocked returns the locked task. Observed on 3.1
   (TK62 "lock", all modes): it returns the task, and while it is locked
   another task that only yields gets no turn (task.c reschedule). */
u16 KAPI LockCurrentTask(u16 lock)
{
    locked_task = lock ? cur_task : 0;
    return locked_task;
}

u16 KAPI IsTaskLocked(void)                                   /* KERNEL.122 */
{
    return locked_task;
}

u16 kmisc_locked_task(void)
{
    return locked_task;
}

/* task t has ended: not the locked task any more (it named the dead TDB:
   KERNEL audit, medium 25). FileCdr's callback stays (observed on 3.1,
   TK64O, TK64P: a task that installs one and ends leaves it, and nobody
   else can remove it; one whose code went with the task goes at the next
   change, filecdr_notify), but for kmisc_app_exit. */
void kmisc_task_end(u16 t)
{
    if (locked_task == t)
        locked_task = 0;
}

/* task t, which has a message queue (USER's InitApp), is ending: before
   USER's signal procedure hears it. USER's AppExit does "if FileCdr(-1)
   is in the ending program's module, FileCdr(0)" (Pietrek, Windows
   Internals ch. 3), in that task, so only when that task installed it.
   Observed on 3.1 (TK64 "helper Q" and TK64M): a program that called
   InitApp and installed its own callback loses it at its end; one that
   installed another module's keeps it. Done here so that it happens
   with w16's USER as with Microsoft's (whose AppExit then finds none). */
void kmisc_app_exit(u16 t)
{
    module FAR *m;
    if (!filecdr_proc || filecdr_task != t)
        return;
    m = module_of((u16)(filecdr_proc >> 16));
    if (m && m->ne_sel == TW(t, TDB_HMOD))
        filecdr_proc = 0;
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

/* Reserved5 (KERNEL.87) = lstrcmp (Wine's name). Observed on 3.1 (TK66
   "klstrcmp", all three runs): the bytes compared after A-Z and C0h-DEh
   but D7h are made lower case ("a" = "A", "_" < "a", "B" > "a", C9h =
   E9h, 8Ah < 9Ah, D7h < F7h); NULL is the empty string (NULL < "a",
   "a" > NULL, NULL = NULL). w16 compared the bytes as they were and
   faulted on NULL (KERNEL audit, medium 43). */
static u8 kl_lower(u8 c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) ?
           (u8)(c + 0x20) : c;
}

int KAPI KLstrcmp(const char FAR *a, const char FAR *b)
{
    static const char none[1] = { 0 };
    if (!a)
        a = none;
    if (!b)
        b = none;
    while (*a && kl_lower((u8)*a) == kl_lower((u8)*b)) {
        a++;
        b++;
    }
    return (int)kl_lower((u8)*a) - (int)kl_lower((u8)*b);
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
            for (i = 0; i < m->seg_count; i++)  /* (not a slot without a */
                if (m->seg[i].sel && (m->seg[i].sel | 7) == (hi | 7))   /* segment: */
                    break;                      /* audit, medium 32) */
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

/* the current task's error mode (SetErrorMode) */
u16 error_mode(void)
{
    return cur_task ? TW(cur_task, TDB_ERRMODE) : boot_errmode;
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
   for the current one. Observed on 3.1 (TK64, both kernels, both modes):
   -1 gives the callback (0 for none); with one installed by another task
   (even one that has ended) it returns 0 and changes nothing, else it
   installs the new one (NULL: none) for the current task and returns 1,
   also for NULL with none installed and for a second callback of the
   same task. The high word is not 0 then on 3.1 (various values). */
u32 KAPI FileCdr(u32 proc)
{
    if (proc == 0xFFFFFFFFUL)
        return filecdr_proc;
    if (filecdr_proc && filecdr_task != cur_task)
        return 0;
    filecdr_proc = proc;
    filecdr_task = cur_task;
    return 1;
}

/* ---- FileCdr's notification (kentry.asm's INT 21h hook calls
   filecdr_notify after a successful change) ----

   Observed on 3.1 (TK64, KRNL286 and KRNL386, Standard and 386 Enhanced
   mode alike):
   - which: INT 21h 39h, 3Ah, 3Ch, 41h, 4301h, 56h, 5Ah, 5Bh and 6Ch
     (any action, also opening a file that is there); not 3Bh, 3Dh (any
     mode), 3Eh, 40h, 45h, 4300h, 47h, 4Eh, 57h (set or get), 68h, nor
     the FCB functions. Through DOS3Call and KERNEL's own _lcreat,
     OpenFile(OF_CREATE, OF_DELETE) the same (they are INT 21h);
     _lopen(OF_WRITE) and OF_EXIST, OF_PARSE nothing.
   - when: after DOS did it, and only when it succeeded (carry clear: a
     delete of a missing file, a create in a missing directory, 5Bh on a
     file that is there call nothing); in the calling task, on its
     stack, as many times as it is nested (the callback's own changes call
     it again, inside); what it returns is not used, and the program gets
     DOS's registers and carry whatever the callback leaves in AX-DX, ES.
   - the code: the caller's AX (3C55h stays 3C55h).
   - the path: the name as the program gave it (DS:DX, for 6Ch DS:SI;
     for 56h the old name; for 5Ah the name DOS made), with "D:" in front
     (the drive letter upper-cased) and, when it does not start with '\'
     or '/', "\", the current directory of that drive (DOS 47h) and "\"
     before it: "c64a.txt" in C:\TK64D is "C:\TK64D\c64a.txt",
     "c:/TK64D/../tk64d/./c64g.txt" is "C:/TK64D/../tk64d/./c64g.txt",
     "C:c64h.txt" "C:\TK64D\c64h.txt", "longname64.text" stays long;
     in the root, "C:\c64t.txt". Not the program's string: a copy.
   - a callback whose code segment is gone (its program ended without
     InitApp, TK64O: Standard mode; 386 Enhanced mode ended TK64 there)
     is removed instead of called.
   UNVERIFIED: 43h with AL other than 0 and 1 (DOS fails them here). */

/* LAR's access rights (bits 8-15), 0 if the selector is not accessible */
static u16 cdr_ar(u16 sel);
#pragma aux cdr_ar = \
    "xor ax, ax" \
    "lar ax, dx" \
    "jz ok" \
    "xor ax, ax" \
    "ok:" \
    parm [dx] value [ax] modify exact [ax];

#define CDR_PATH    160
#define CDR_DEPTH   4
static char cdr_buf[CDR_DEPTH][CDR_PATH];       /* one per nesting level */
static u16 cdr_depth;

static void cdr_put(char *b, u16 *n, const char FAR *s)
{
    while (*s && *n < CDR_PATH - 1)
        b[(*n)++] = *s++;
    b[*n] = 0;
}

void __cdecl __far filecdr_notify(u16 code, const char FAR *path)
{
    typedef void (__far __pascal *cdrproc)(u16 code, const char FAR *path);
    u32 p = filecdr_proc;
    u16 n = 0, drive;
    char *b;
    regs16 r;

    if (!p)
        return;
    /* the callback's code must still be there (and be code) */
    if ((cdr_ar((u16)(p >> 16)) & 0x1800) != 0x1800) {
        filecdr_proc = 0;
        return;
    }
    b = cdr_buf[cdr_depth < CDR_DEPTH ? cdr_depth : CDR_DEPTH - 1];
    if (path[0] && path[1] == ':') {
        drive = (u8)path[0];
        if (drive >= 'a' && drive <= 'z')
            drive -= 32;
        path += 2;
    } else {
        r.ax = 0x1900;
        r.bx = r.cx = r.dx = r.si = r.di = 0;
        r.ds = r.es = 0;
        dos21(&r);
        drive = 'A' + (r.ax & 0xFF);
    }
    b[n++] = (char)drive;
    b[n++] = ':';
    if (path[0] != '\\' && path[0] != '/') {
        b[n++] = '\\';
        b[n] = 0;
        r.ax = 0x4700;
        r.dx = (drive - 'A' + 1) & 0xFF;
        r.si = FP_OFF((char FAR *)(b + n));
        r.ds = FP_SEG((char FAR *)(b + n));
        r.bx = r.cx = r.di = r.es = 0;
        if (dos21(&r))
            b[n] = 0;
        while (b[n] && n < CDR_PATH - 2)
            n++;
        if (b[n - 1] != '\\')
            b[n++] = '\\';
        b[n] = 0;
    }
    cdr_put(b, &n, path);
    cdr_depth++;
    ((cdrproc)p)(code, (const char FAR *)b);
    cdr_depth--;
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

/* IsWinOldApTask (KERNEL.158): TRUE for a task of WINOLDAP, the module
   that runs a DOS program (WINOLDAP.MOD, WINOA386.MOD: module name
   WINOLDAP). Windows 3.1's Task List asks it at End Task (Schulman,
   Undocumented Windows ch. 1; ARCHITECTURE.md 15.5). Schulman's listing
   shows 3.1 reading bit 0 of a flag word at 48h of the task's PDB, which
   its loader sets for WINOLDAP; w16 asks the task's module instead (the
   same tasks). 0 for anything that is not a task. */
u16 KAPI_ES IsTask(u16 h);
u16 KAPI IsWinOldApTask(u16 htask)
{
    module FAR *m;
    if (!htask || !IsTask(htask))
        return 0;
    m = module_of(*(u16 FAR *)MK_FP(htask, 0x1E));       /* TDB: hModule */
    return m && kstricmp(m->name, "WINOLDAP") == 0;
}


/* GetExpWinVer (KERNEL.167): the expected Windows version in the NE
   header (3Eh) of hModule or hInstance, found as GetExePtr finds it
   (TK83: 030Ah for the instance block of a module table a program made;
   Microsoft's USER asks it in LoadBitmap and takes the bitmaps of a
   module below 3.0 for Windows 2's: Win32s's CARDS.DLL's were read so
   and Freecell said "Out of memory"). */
u16 KAPI GetExpWinVer(u16 h)
{
    module FAR *m = module_of_exe(h);
    return m ? NE_HDR(m)->expver : 0;
}

/* DirectResAlloc (KERNEL.168): AllocResource without a resource
   (Schulman: owned by the module of hInstance, through GetExePtr).
   Observed (TK62 "directres"): a moveable block owned by the module;
   the flags are a resource's, 1000h making it discardable (0 and 10h do
   not). w16 gave the task a plain moveable block (KERNEL audit, L27).
   UNVERIFIED: an hInstance that is no module's (w16: the caller's
   block, as before). */
u16 KAPI DirectResAlloc(u16 hinst, u16 flags, u16 size)
{
    module FAR *m = module_of_exe(hinst);
    u16 f = GMEM_MOVEABLE | ((flags & 0x1000) ? GMEM_DISCARDABLE : 0);
    if (!m)
        return GlobalAlloc(f, size);
    return galloc(f, size, m->ne_sel, NULL);
}


/* LongPtrAdd (KERNEL.180): selector.c. Observed (tests/apps/tk17.c), it
   moves the selector's base rather than stepping by __AHINCR. */

/* WriteOutProfiles (KERNEL.315): w16 writes profiles through at once. */
void KAPI WriteOutProfiles(void)
{
}

/* IsRomModule (KERNEL.323): khook.asm, with IsRomFile. */

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

/* the highest offset IsBad*Ptr take for sel: its limit; for an
   expand-down data segment FFFFh, every offset (observed on 3.1, TK62
   "badptr": an expand-down copy of DS with limit FFFh was good at 10h,
   2000h and FFF0h; w16 took the limit as the top: KERNEL audit, H22) */
static u16 top_of(u16 sel, u8 ar)
{
    return (ar & 0x0C) == 0x04 ? 0xFFFF : sel_lim16(sel);
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
    return off + n - 1 <= top_of(sel, ar);
}

/* how many bytes from p on can be read (write 0) or written (write 1)
   before the segment's limit: 0 for a bad selector (string.c, atom.c:
   copying up to where 3.1's would fault, US27) */
u32 ptr_room(const void FAR *p, int write)
{
    u16 sel = FP_SEG(p), off = FP_OFF(p);
    if (!range_ok(sel, off, 1, write))
        return 0;
    return (u32)top_of(sel, rights(sel)) - off + 1;
}

/* Observed (TK62 "badptr", 3.1 both modes): 0 bytes are good wherever
   they are, NULL too (w16 said bad: KERNEL audit, H22) */
u16 KAPI_ES IsBadReadPtr(void FAR *p, u16 n)
{
    return n && !range_ok(FP_SEG(p), FP_OFF(p), n, 0);
}

u16 KAPI IsBadWritePtr(void FAR *p, u16 n)
{
    return n && !range_ok(FP_SEG(p), FP_OFF(p), n, 1);
}

u16 KAPI IsBadCodePtr(void FAR *p)
{
    u8 ar = rights(FP_SEG(p));
    return !((ar & 0x80) && (ar & 0x18) == 0x18 && FP_OFF(p) <= sel_lim16(FP_SEG(p)));
}

/* Observed (TK62 "strptr", 3.1 both modes and KRNL286): the NUL must be
   in the first max bytes ("abc" is bad with 3, good with 4; max 0 is
   always bad), but FFFFh sets no bound (64K without a NUL is good). w16
   took a string that went on past max as good (KERNEL audit, H28). */
u16 KAPI IsBadStringPtr(const char FAR *p, u16 max)
{
    u16 sel = FP_SEG(p), off = FP_OFF(p), lim, i;
    if (!range_ok(sel, off, 1, 0))
        return 1;
    lim = top_of(sel, rights(sel));
    for (i = 0; i < max; i++) {
        if ((u32)off + i > lim)
            return 1;
        if (!p[i])
            return 0;
    }
    return max != 0xFFFF;
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
   returns dst. 3.1's lets a bad pointer fault and recovers (its "__GP"
   table): what it copied before the fault stays (US27: USER's
   DlgDirSelect into a buffer 4 bytes from a segment's end left the 4
   bytes "FILE" there and returned 0). w16 copies as far and returns
   NULL. */
char FAR * KAPI lstrcpyn(char FAR *dst, const char FAR *src, u16 n)
{
    u16 i = 0;
    u32 rd, wr;
    if (!n)
        return dst;
    rd = ptr_room(src, 0);
    wr = ptr_room(dst, 1);
    for (; i + 1 < n; i++) {
        if (i >= rd)
            return NULL;
        if (!src[i])
            break;
        if (i >= wr)
            return NULL;
        dst[i] = src[i];
    }
    if (i >= wr)
        return NULL;
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
