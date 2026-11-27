/*
 * kmisc2.c - KERNEL exports Windows 3.1 has that w16 lacked (docs/exports/
 * KERNEL.txt against kernel.lnk), each observed on 3.1 first with
 * tests/apps/tk50.c (as the shell, Microsoft's KRNL286 and KRNL386:
 * tests/dropin.sh -r, tests/refrun.sh). The register-level ones that keep
 * every register (no-ops on 3.1) are in khook.asm; THHOOK and the
 * notification hooks in toolhelp.c.
 *
 * Sources for the contracts: the Windows 3.1 SDK (hmemcpy, IsDBCSLeadByte,
 * GetCodeHandle, SwitchStackTo/Back, LimitEmsPages, SetSwapAreaSize,
 * Get/SetWinDebugInfo); Schulman et al., "Undocumented Windows" (1992)
 * for the undocumented ones; names from Wine's X11-licensed spec files.
 * Facts only; every result below was observed unless marked UNVERIFIED.
 */
#include "kernel.h"

u16 KAPI GetExePtrC(u16 h);
u32 KAPI GlobalHandle(u16 sel);

/* ---- hmemcpy (KERNEL.348) ---- */

static void fwd(u16 ds, u16 doff, u16 ss, u16 soff, u16 n);
#pragma aux fwd = \
    "push ds" \
    "mov ds, dx" \
    "cld" \
    "rep movsb" \
    "pop ds" \
    parm [es] [di] [dx] [si] [cx] modify exact [si di cx];

static void bwd(u16 ds, u16 doff, u16 ss, u16 soff, u16 n);
#pragma aux bwd = \
    "push ds" \
    "mov ds, dx" \
    "std" \
    "rep movsb" \
    "cld" \
    "pop ds" \
    parm [es] [di] [dx] [si] [cx] modify exact [si di cx];

/* a huge pointer moved on by n bytes (__AHINCR per 64K) */
static void hadd(u16 *sel, u16 *off, u32 n)
{
    u32 o = (u32)*off + n;
    *sel += (u16)(o >> 16) * sel_inc;
    *off = (u16)o;
}

/* Observed (TK50): copies across 64K boundaries, overlapping blocks
   correctly in both directions (memmove); DX:AX = 0; keeps SI, DI, BP,
   DS, clears the carry (BX, CX, DX, ES changed on 3.1). */
u32 KAPI hmemcpy(void FAR *dst, const void FAR *src, u32 n)
{
    u16 ds = FP_SEG(dst), doff = FP_OFF(dst), ss = FP_SEG(src), soff = FP_OFF(src);
    u32 dl, sl;
    if (!n)
        return 0;
    dl = dpmi_get_base(ds) + doff;
    sl = dpmi_get_base(ss) + soff;
    if (dl > sl && dl < sl + n) {
        /* backwards, from the last byte */
        hadd(&ds, &doff, n - 1);
        hadd(&ss, &soff, n - 1);
        while (n) {
            u16 c = (doff < soff ? doff : soff);
            u32 k = (u32)c + 1;
            if (k > n)
                k = n;
            if (k == 0x10000UL) {       /* one byte, then the rest */
                bwd(ds, doff, ss, soff, 1);
                k = 1;
            } else {
                bwd(ds, doff, ss, soff, (u16)k);
            }
            n -= k;
            if (!n)
                break;
            /* step back k bytes */
            if (doff >= k) {
                doff -= (u16)k;
            } else {
                doff = (u16)(doff - k);
                ds -= sel_inc;
            }
            if (soff >= k) {
                soff -= (u16)k;
            } else {
                soff = (u16)(soff - k);
                ss -= sel_inc;
            }
        }
        return 0;
    }
    while (n) {
        u32 room = 0x10000UL - (doff > soff ? doff : soff);
        u32 k = room < n ? room : n;
        if (k == 0x10000UL) {
            fwd(ds, doff, ss, soff, 0x8000);
            fwd(ds, doff + 0x8000, ss, soff + 0x8000, 0x8000);
        } else {
            fwd(ds, doff, ss, soff, (u16)k);
        }
        n -= k;
        hadd(&ds, &doff, k);
        hadd(&ss, &soff, k);
    }
    return 0;
}

/* ---- simple ones ---- */

/* IsDBCSLeadByte (KERNEL.207): 0 for every byte on a single-byte (US)
   system; keeps every register but AX, clears the carry. */
u16 KAPI IsDBCSLeadByte(u16 c)
{
    (void)c;
    return 0;
}

/* GetFreeMemInfo (KERNEL.316): KRNL286 returns -1 (Schulman; observed
   under KRNL286 and, in Standard mode, KRNL386). */
u32 KAPI GetFreeMemInfo(void)
{
    return 0xFFFFFFFFUL;
}

/* GetLastDiskChange (KERNEL.98): 0 (observed: AX = 0). */
u16 KAPI GetLastDiskChange(void)
{
    return 0;
}

/* MemoryFreed (KERNEL.126): 0 for 0 and 10h (observed, DX too). */
u32 KAPI MemoryFreed(u16 n)
{
    (void)n;
    return 0;
}

/* LimitEmsPages (KERNEL.156): no EMS in protected mode: DX:AX = 0. */
u32 KAPI LimitEmsPages(u32 kb)
{
    (void)kb;
    return 0;
}

/* SwapRecording (KERNEL.204): AX = 0 (DX: unrelated, 0167h). */
u16 KAPI KSwapRecording(u16 flag)
{
    (void)flag;
    return 0;
}

/* Get/SetWinDebugInfo (KERNEL.355, 356): only the debugging KERNEL has
   the settings: 0, the buffer untouched (observed). */
u16 KAPI GetWinDebugInfo(void FAR *info, u16 flags)
{
    (void)info;
    (void)flags;
    return 0;
}

u16 KAPI SetWinDebugInfo(void FAR *info)
{
    (void)info;
    return 0;
}

/* ---- Reserved3/4 (KERNEL.79, 80): Windows 2.x's AnsiUpper and
   AnsiLower. Observed: with a zero selector the low word is a character,
   returned converted (DX = 0); otherwise the string is converted in place
   and DX:AX is the pointer. Windows ANSI: E9h <-> C9h. Observed for
   80h-FFh (TK62 "ansi", 3.1 both modes): E0h-FEh but F7h <-> C0h-DEh but
   D7h, and 9Ah <-> 8Ah, 9Ch <-> 8Ch, FFh <-> 9Fh (w16 lacked the last
   three pairs: KERNEL audit, low). */

static u8 up(u8 c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
        return (u8)(c - 0x20);
    if (c == 0x9A || c == 0x9C)
        return (u8)(c - 0x10);
    return c == 0xFF ? 0x9F : c;
}

static u8 down(u8 c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
        return (u8)(c + 0x20);
    if (c == 0x8A || c == 0x8C)
        return (u8)(c + 0x10);
    return c == 0x9F ? 0xFF : c;
}

static u32 ansi_case(u32 p, int upper)
{
    u8 FAR *s;
    if (!(p >> 16))
        return upper ? up((u8)p) : down((u8)p);
    for (s = (u8 FAR *)p; *s; s++)
        *s = upper ? up(*s) : down(*s);
    return p;
}

u32 KAPI KAnsiUpper(u32 p)
{
    return ansi_case(p, 1);
}

u32 KAPI KAnsiLower(u32 p)
{
    return ansi_case(p, 0);
}

/* ---- modules and segments ---- */

/* IsSharedSelector (KERNEL.345): whether the selector belongs to a DLL.
   Observed: 1 for KERNEL's code and data, KERNEL's and USER's hModule;
   0 for a program's DS and CS and for an hTask. (3.1 faults on 0.) */
u16 KAPI KIsSharedSelector(u16 sel)
{
    u16 h = GetExePtrC(sel);
    module FAR *m = h ? module_of(h) : NULL;
    return m && (m->flags & NEF_LIBRARY) ? 1 : 0;
}

/* GetCodeHandle (KERNEL.93): for a procedure address (or any address in
   a loaded segment): AX = the segment's handle, DX = its selector
   (observed, also for a data address); MAKELONG(segment number, hModule)
   names segment n of the module (Schulman). 0 gives DX = 0. w16 loads
   every segment with its module, so nothing is loaded here. */
u32 KAPI KGetCodeHandle(u32 proc)
{
    u16 hi = (u16)(proc >> 16), lo = (u16)proc;
    module FAR *m;
    u32 gh;
    if (!hi)
        return 0;
    m = module_of(hi);
    if (m && (m->ne_sel | 1) == (hi | 1)) {
        if (!lo || lo > m->seg_count)
            return 0;
        return ((u32)m->seg[lo - 1].sel << 16) | m->seg[lo - 1].hnd;
    }
    if (m) {                    /* KERNEL's segments are not in the heap */
        u16 i;
        for (i = 0; i < m->seg_count; i++)
            if ((m->seg[i].sel | 3) == (hi | 3))
                /* AX = GlobalHandle's, as on 3.1 (TK50): the segment's
                   selector (TK62 "codehandle"; global.c) */
                return ((u32)m->seg[i].sel << 16) | (u16)GlobalHandle(hi);
    }
    gh = GlobalHandle(hi);
    if (!(u16)gh)
        return 0;
    return ((u32)(hi | 1) << 16) | (u16)gh;
}

/* OpenPathname (KERNEL.75): Schulman: DOS open (INT 21h 3Dh) with the
   access mode; observed: AX = the DOS handle, FFFFh if it fails. */
u16 KAPI KOpenPathname(const char FAR *path, u16 mode)
{
    regs16 r;
    r.ax = 0x3D00 | (mode & 0xFF);
    r.dx = FP_OFF(path);
    r.ds = FP_SEG(path);
    r.es = 0;
    r.bx = r.cx = r.si = r.di = 0;
    return dos21(&r) ? 0xFFFF : r.ax;
}

/* PatchCodeHandle (KERNEL.110): Schulman: patch the prologues of the
   exported procedures in a code segment, as the loader does. Observed
   (TK50): for a program's code segment AX = its handle, DX = its
   selector, and an exported "push ds / pop ax / nop" prologue reads "nop
   nop nop" afterwards; 0 gives DX:AX = 0. w16's loader patches every
   exported prologue when it loads the module (loader.c), so nothing is
   left to do here. */
u32 KAPI PatchCodeHandle(u16 h)
{
    u32 gh;
    if (!h)
        return 0;
    gh = GlobalHandle(h);
    if (!(u16)gh)
        return 0;
    return gh;
}

/* SetSwapAreaSize (KERNEL.106, SDK): AX = the paragraphs granted for
   code, DX = the most there could be. Observed under KRNL286 (TK50): (0)
   gave 0, FFFFh; (100h) 100h, FFFDh (KRNL386: 100h, 0); code is never
   swapped here. UNVERIFIED: DX in general (w16: FFFFh), larger requests. */
u32 KAPI SetSwapAreaSize(u16 paras)
{
    return 0xFFFF0000UL | paras;
}

/* ---- ending a program ---- */

u32 KAPI GetProcAddress(u16 h, const char FAR *name);
typedef u16 (__far __pascal *syserrbox_fn)(const char FAR *msg,
                                           const char FAR *title,
                                           u16 b1, u16 b2, u16 b3);

/* FatalAppExit (KERNEL.137, SDK): observed on 3.1 (tests/apps/tk52.c,
   screenshots, as the shell): with a message queue the program gets the
   box USER's SysErrorBox (USER.320) draws for (msg, "Application Error",
   0, 8000h | 8, 0), one "Close" button (button code 8: the same box with
   another title, TK52 's'); it does not return, and the program ends
   once the box is closed. Without a queue no box appeared and Windows
   (the program was the shell) ended. UNVERIFIED: the exit code (w16:
   FFh, as FatalExit), fuAction (ignored), what happens without USER. */
void KAPI FatalAppExit(u16 action, const char FAR *msg)
{
    regs16 r;
    module FAR *u = module_find("USER");
    (void)action;
    if (u && cur_task && *(u16 FAR *)MK_FP(cur_task, 0x20)) {
        syserrbox_fn box = (syserrbox_fn)GetProcAddress(u->ne_sel,
                                                        (const char FAR *)MK_FP(0, 320));
        if (box)
            box(msg ? msg : "", "Application Error", 0, 0x8008, 0);
    }
    if (!cur_task) {
        /* no task to end (a driver's or DLL's initialisation during the
           boot): Windows ends with the message on the text screen, as
           other reports that end it, not by a DOS exit of KERNEL's own
           process past its clean-up (KERNEL audit, L26) */
        text_mode();
        kputs("w16: FatalAppExit: ");
        if (msg)
            kputs(msg);
        kputs("\r\n");
        kexit_to_dos(0xFF);
    }
    r.ax = 0x4CFF;              /* ends the task through our INT 21h hook */
    r.ds = r.es = 0;
    r.bx = r.cx = r.dx = r.si = r.di = 0;
    dos21(&r);
}

/* UndefDynlink (KERNEL.120): Schulman: what a call to an import that
   could not be resolved reaches. Observed (TK52): the FatalAppExit box
   with "Call to Undefined Dynalink". */
void KAPI UndefDynlink(void)
{
    FatalAppExit(0, "Call to Undefined Dynalink");
}

/* KERNEL's "File Error" box for a library that cannot be found: observed
   on Windows 3.1 (tests/apps/tk59.c, as the shell, screenshots; boot-test
   case missingdll) for LoadLibrary("NOSUCH.DLL") and for WinExec or
   LoadModule of a program importing a DLL that is missing ("Cannot find
   OLECLI.DLL": the module reference's name with .DLL); the call then
   returns 2; for LoadLibrary("nosuch") "Cannot find NOSUCH" (upper case,
   no extension added), for LoadLibrary("C:\\NODIR\\NOSUCH.DLL") (3, path
   not found) "Cannot find NOSUCH.DLL" (the base name). Not for a program
   that is itself missing (WinExec, LoadModule), nor with
   SetErrorMode(SEM_NOOPENFILEERRORBOX, 8000h). It is OpenFile's
   OF_PROMPT box (cannot_find_box, below): Pietrek (LMLoadExeFile), the
   loader opens a DLL with OF_PROMPT | OF_CANCEL, without OF_PROMPT under
   SEM_NOOPENFILEERRORBOX. The box is USER's
   SysErrorBox("Cannot find NAME", "File Error", 0, 8000h | 8, 0): one
   Close button, as FatalAppExit's (the same layout, pixel for pixel).
   UNVERIFIED: during the boot (w16: no box); upper-casing beyond a-z. */
void file_error_box(const char FAR *name, const char *ext)
{
    if (!(error_mode() & 0x8000))
        cannot_find_box(name, ext);
}

/* OpenFile's OF_PROMPT box, observed on 3.1 (TK59 steps 10-13, boot-test
   case missingdll@tk59: screenshots and return codes): for a file that
   is not found (DOS error 2, and 3 for a directory that is not there),
   with OF_PROMPT, with or without OF_CANCEL, the same "File Error" box
   as above with the name given (its base name, "Cannot find
   NOSUCH.TXT" for C:\NODIR\NOSUCH.TXT), one Close button (Escape does
   not close it); also under SetErrorMode(8000h); OpenFile then fails as
   without it (HFILE_ERROR, nErrCode 2 or 3). OF_CANCEL changes nothing
   there (the SDK: it adds a Cancel button). w16 ignored OF_PROMPT
   (KERNEL audit, L32). UNVERIFIED: other errors (sharing, no handle
   left); OF_CREATE. */
void cannot_find_box(const char FAR *name, const char *ext)
{
    char t[16 + DOS_PATHMAX];
    const char FAR *base;
    u16 n, i;
    module FAR *u;
    if (!winboot_done || !cur_task)
        return;
    u = module_find("USER");
    if (!u)
        return;
    for (base = name, i = 0; name[i]; i++)      /* the base name only */
        if (name[i] == '\\' || name[i] == '/' || name[i] == ':')
            base = name + i + 1;
    kmemcpy(t, "Cannot find ", 12);
    n = 12;
    for (i = 0; base[i] && n < sizeof t - 6; i++)
        t[n++] = base[i];
    for (i = 0; ext && ext[i] && n < sizeof t - 1; i++)
        t[n++] = ext[i];
    t[n] = 0;
    for (i = 12; i < n; i++)            /* upper case */
        if (t[i] >= 'a' && t[i] <= 'z')
            t[i] -= 'a' - 'A';
    {
        syserrbox_fn box = (syserrbox_fn)GetProcAddress(u->ne_sel,
                                                        (const char FAR *)MK_FP(0, 320));
        if (box)
            box(t, "File Error", 0, 0x8008, 0);
    }
}

/* DebugBreak (KERNEL.203, SDK): a breakpoint for a debugger. Observed
   (TK52): with no debugger the retail KERNEL returns and the program
   goes on. UNVERIFIED: what a debugger (TOOLHELP's InterruptRegister)
   gets; w16 raises nothing. */
void KAPI DebugBreak(void)
{
}
