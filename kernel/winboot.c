/*
 * winboot.c - booting Windows: what KERNEL does after itself when it runs
 * as WINDOWS\SYSTEM\KRNL286.EXE (started by WIN.COM through DOSX).
 *
 * Observed on Windows 3.1 Standard mode (ARCHITECTURE.md 9.4; tests/
 * dropin.sh -r writes the reference BOOTLOG.TXT, tests/apps/tk22.c):
 *   - KERNEL's command tail is what WIN.COM leaves of its own: the
 *     switches it handled blanked out ("WIN /S /B X Y" gives
 *     "     /B X Y"). /B is KERNEL's: it writes WINDOWS\BOOTLOG.TXT,
 *     starting with "[boot]", then "LoadStart = NAME" before and
 *     "LoadSuccess = NAME" or "LoadFail = NAME Failure code is NN" after
 *     each load while Windows boots (the [boot] modules, what their
 *     initialisation loads, the shell and the DLLs it imports; nothing
 *     after the boot). DiagOutput (KERNEL.340) appends a string as it
 *     is (USER writes "INIT=Keyboard\r\n" etc. through it).
 *   - The order: SYSTEM.INI [boot] system.drv, keyboard.drv, mouse.drv,
 *     display.drv, sound.drv, comm.drv, fonts.fon, oemfonts.fon (each
 *     logged under its value there, e.g. "vga.drv"), then GDI.EXE and
 *     USER.EXE (logged so), then the shell (shell=, logged as written),
 *     whose command tail is the rest of KERNEL's.
 *   - A module that does not load: "Error loading NAME" on the screen
 *     and back to DOS.
 * Pietrek (ch. 1, LoadNewExe, SlowBoot) describes the same sequence; the
 * default names for missing keys come from there (UNVERIFIED).
 *
 * After USER, KERNEL calls USER's FinalUserInit (USER.400, Pietrek) once
 * the shell is loaded: BOOTLOG.TXT shows "INIT=Final USER" right after
 * "LoadSuccess = progman.exe".
 */
#include "kernel.h"

int winboot_active;             /* booting Windows (loader.c) */
static int diag;                /* writing BOOTLOG.TXT */
static int booting;             /* still logging loads */
static char logpath[DOS_PATHMAX + 16];

int KAPI GetPrivateProfileString(const char FAR *sec, const char FAR *key,
                                 const char FAR *def, char FAR *buf, u16 n,
                                 const char FAR *file);
u16 KAPI_ES GetWindowsDirectory(char FAR *buf, u16 n);
u16 KAPI LoadLibrary(const char FAR *name);
u32 KAPI GetProcAddress(u16 h, const char FAR *name);
u16 KAPI GetModuleHandle(const char FAR *name);
u16 KAPI GetPrivateProfileInt(const char FAR *sec, const char FAR *key, u16 def,
                              const char FAR *file);

/* ---- the boot log ---- */

static u16 dos(u16 ax, u16 bx, u16 cx, u16 dx, u16 ds, int *cf)
{
    regs16 r;
    r.ax = ax;
    r.bx = bx;
    r.cx = cx;
    r.dx = dx;
    r.ds = ds;
    r.es = 0;
    r.si = r.di = 0;
    *cf = dos21(&r);
    return r.ax;
}

/* Append s to BOOTLOG.TXT: open, write, close each time, so that what
   was written survives a crash (Pietrek, DiagOutput). */
static void diag_write(const char FAR *s)
{
    int cf;
    u16 fh;
    wd_progress();
    if (!diag)
        return;
    fh = dos(0x3D22, 0, 0, FP_OFF((char FAR *)logpath),
             FP_SEG((char FAR *)logpath), &cf);
    if (cf)
        return;
    dos(0x4202, fh, 0, 0, 0, &cf);
    dos(0x4000, fh, kstrlen(s), FP_OFF(s), FP_SEG(s), &cf);
    dos(0x3E00, fh, 0, 0, 0, &cf);
}

/* DiagQuery (KERNEL.339, toolhelp.c): writing BOOTLOG.TXT? */
int diag_query(void)
{
    return diag;
}

static void diag_init(void)
{
    int cf;
    u16 n = GetWindowsDirectory(logpath, DOS_PATHMAX), fh;
    const char *f = "BOOTLOG.TXT";
    if (n && logpath[n - 1] != '\\')
        logpath[n++] = '\\';
    while (*f)
        logpath[n++] = *f++;
    logpath[n] = 0;
    fh = dos(0x3D22, 0, 0, FP_OFF((char FAR *)logpath),
             FP_SEG((char FAR *)logpath), &cf);
    if (cf)
        fh = dos(0x3C00, 0, 0, FP_OFF((char FAR *)logpath),
                 FP_SEG((char FAR *)logpath), &cf);
    if (cf)
        return;
    dos(0x3E00, fh, 0, 0, 0, &cf);
    diag = 1;
    diag_write("[boot]\r\n");
}

/* DiagOutput (KERNEL.340): observed on 3.1 (TK22) to append the string
   to BOOTLOG.TXT as it is, also after the boot. */
void KAPI DiagOutput(const char FAR *s)
{
    diag_write(s);
}

/* the loader's hooks (loader.c) */
void diag_load_start(const char FAR *name)
{
    if (!booting)
        return;
    diag_write("LoadStart = ");
    diag_write(name);
    diag_write("\r\n");
}

void diag_load_end(const char FAR *name, u16 rc)
{
    char code[3];
    if (!booting)
        return;
    if (rc >= 32) {
        diag_write("LoadSuccess = ");
        diag_write(name);
        diag_write("\r\n");
        return;
    }
    code[0] = "0123456789ABCDEF"[(rc >> 4) & 15];   /* UNVERIFIED: hex */
    code[1] = "0123456789ABCDEF"[rc & 15];
    code[2] = 0;
    diag_write("LoadFail = ");
    diag_write(name);
    diag_write(" Failure code is ");
    diag_write(code);
    diag_write("\r\n");
}

/* ---- the import trace (thunk.c): [w16] trace=MOD.ORD,... ---- */

static char trace_list[2048];

int trace_wanted(const char FAR *mod, u16 ordinal)
{
    const char *p = trace_list;
    while (*p) {
        const char FAR *m = mod;
        u16 n = 0;
        while (*m && *p && *p != '.' &&
               ((*m | 0x20) == (*p | 0x20)))
            m++, p++;
        if (!*m && *p == '.') {
            p++;
            while (*p >= '0' && *p <= '9')
                n = n * 10 + (*p++ - '0');
            if (n == ordinal)
                return 1;
        }
        while (*p && *p != ',')
            p++;
        if (*p == ',')
            p++;
    }
    return 0;
}

/* [w16] modmap=1 (a debugging aid): every module's segment selectors on
   standard output once the shell is loaded */
static void print_modmap(void)
{
    module FAR *m;
    u16 i;
    for (m = module_first; m; m = m->next) {
        kdbg("w16: ");
        kdbg(m->name);
        kdbg(" rec=");
        kdbghex(m->self);
        kdbg(" hmod=");
        kdbghex(m->ne_sel);
        for (i = 0; i < m->seg_count; i++) {
            kdbg(" ");
            kdbghex(i + 1);
            kdbg("=");
            kdbghex(m->seg[i].sel);
        }
        kdbg(" ");
        kdbg(m->path);
        kdbg("\r\n");
    }
}

/* ---- the boot ---- */

static const char sysini[] = "SYSTEM.INI";

/* Is this a Windows boot? w16's own rule: the Windows directory has a
   SYSTEM.INI with a [boot] section that names the system driver.
   Otherwise KERNEL runs the program named on its command line (the M1
   test mode, ARCHITECTURE.md 9.1). */
int winboot_wanted(void)
{
    char buf[16];
    return GetPrivateProfileString("boot", "system.drv", "", buf, sizeof buf,
                                   sysini) > 0;
}

static void boot_fail(const char FAR *name)
{
    text_mode();
    kputs("\r\n\r\nError loading ");
    kputs(name);
    kputs("\r\n");
    trace_print();                      /* w16's [w16] trace, if any */
    if (GetPrivateProfileInt("w16", "modmap", 0, sysini))
        print_modmap();
    kexit_to_dos(1);                    /* UNVERIFIED: the exit code */
}

/* Load one boot module: SYSTEM.INI [boot] key (if any) names its file. */
static u16 boot_load(const char *key, const char *def)
{
    static char name[DOS_PATHMAX];
    u16 h;
    if (key)
        GetPrivateProfileString("boot", key, def, name, sizeof name, sysini);
    else
        kmemcpy(name, def, kstrlen(def) + 1);
    h = LoadLibrary(name);              /* logs LoadStart, LoadSuccess */
    if (h < 32)
        boot_fail(name);
    return h;
}

/* Remove /B or -B from KERNEL's command tail (blanked, as WIN.COM does
   with its own switches); returns whether it was there. UNVERIFIED:
   only a /B word on its own is KERNEL's. */
static int take_switch_b(void)
{
    u8 FAR *psp = MK_FP(psp_sel, 0);
    u16 i, len = psp[0x80], found = 0;
    for (i = 0; i + 1 < len; i++) {
        u8 c = psp[0x81 + i], d = psp[0x82 + i];
        if ((c == '/' || c == '-') && (d == 'b' || d == 'B') &&
            (i == 0 || psp[0x80 + i] == ' ' || psp[0x80 + i] == '\t') &&
            (i + 2 >= len || psp[0x83 + i] == ' ' || psp[0x83 + i] == '\t')) {
            psp[0x81 + i] = psp[0x82 + i] = ' ';
            found = 1;
        }
    }
    return found;
}

/* USER's FinalUserInit (USER.400, Pietrek) */
static void final_user_init(void)
{
    u32 p = GetProcAddress(GetModuleHandle("USER"), "#400");
    if (p)
        ((void (__far __pascal *)(void))p)();
}

u16 winboot(void)
{
    static char shell[DOS_PATHMAX];
    static const u16 show[2] = { 2, 1 };
    u8 FAR *psp = MK_FP(psp_sel, 0);
    u8 tail[0x80];
    module FAR *m;
    u16 i, n, err, ad, libinit = 0;

    winboot_active = 1;
    fault_init();
    GetPrivateProfileString("w16", "trace", "", trace_list, sizeof trace_list,
                            sysini);
    task_boot_tdb();
    wd_start(GetPrivateProfileInt("w16", "watchdog", 0, sysini));
    {
        extern u16 sched_log, exit_msg, idle_1689;
        sched_log = GetPrivateProfileInt("w16", "schedlog", 0, sysini);
        idle_1689 = GetPrivateProfileInt("w16", "idle1689", 0, sysini);
        heap_check_on = GetPrivateProfileInt("w16", "heapcheck", 0, sysini);
        exit_msg = GetPrivateProfileInt("w16", "exitmsg", 0, sysini);
        {
            extern u16 debug_to_aux;
            debug_to_aux = GetPrivateProfileInt("w16", "DebugToAux", 0, sysini) != 0;
        }
    }
    {
        /* [w16] debuglog=FILE: w16's own diagnostics (util.c kdbg) */
        static char dbg[80];
        GetPrivateProfileString("w16", "debuglog", "", dbg, sizeof dbg, sysini);
        kdbg_init(dbg);
    }
    if (take_switch_b())
        diag_init();
    booting = 1;
    boot_load("system.drv", "SYSTEM.DRV");
    boot_load("keyboard.drv", "KEYBOARD.DRV");
    boot_load("mouse.drv", "MOUSE.DRV");
    boot_load("display.drv", "DISPLAY.DRV");
    boot_load("sound.drv", "SOUND.DRV");
    boot_load("comm.drv", "COMM.DRV");
    boot_load("fonts.fon", "FONTS.FON");
    boot_load("oemfonts.fon", "OEMFONTS.FON");
    boot_load(NULL, "GDI.EXE");
    win_screen = 1;                     /* USER's initialisation enables
                                           the display: graphics from here */
    boot_load(NULL, "USER.EXE");

    /* the shell: its command tail is the rest of KERNEL's, without the
       leading blanks (UNVERIFIED) */
    GetPrivateProfileString("boot", "shell", "PROGMAN.EXE", shell, sizeof shell,
                            sysini);
    n = psp[0x80] < 0x7E ? psp[0x80] : 0x7E;
    for (i = 0; i < n && (psp[0x81 + i] == ' ' || psp[0x81 + i] == '\t'); i++)
        ;
    tail[0] = (u8)(n - i);
    kmemcpy(tail + 1, psp + 0x81 + i, n - i);
    tail[1 + n - i] = '\r';
    diag_load_start(shell);
    m = module_load_program_init(shell, &err, &libinit);
    thhook_set_shell(m ? m->ne_sel : 0);   /* THHOOK 28h (toolhelp.c) */
    ad = m ? NE_HDR(m)->auto_data : 0;
    if (m && (!ad || ad > m->seg_count ||
              !task_create(m, m->seg[ad - 1].sel, m->seg[ad - 1].hnd, 0,
                           (const u8 FAR *)tail, (const u16 FAR *)show, libinit)))
        err = 11;                       /* UNVERIFIED */
    else if (m)
        err = m->seg[ad - 1].hnd;
    diag_load_end(shell, err);
    if (err < 32)
        boot_fail(shell);
    booting = 0;
    if (GetPrivateProfileInt("w16", "modmap", 0, sysini))
        print_modmap();
    final_user_init();
    /* the boot is over: the watchdog ([w16] watchdog=SECONDS) watches the
       boot only. It kept running and, SECONDS after the last boot log line,
       reported "no boot progress" and ended a Windows that was only idle:
       tests/dropin.sh -p 286 with a fixed-time screenshot (DROPIN_FIXED=1,
       -t 150) showed a black screen, which looked like a boot failure on
       the 80286 */
    wd_stop();
    task_boot_tdb_end();
    {
        u16 rc = task_boot();           /* back when the last task ends */
        trace_print();
        return rc;
    }
}
