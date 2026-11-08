/*
 * kmain.c - KERNEL initialisation (M1).
 *
 * Usage (from DOS, with a DPMI host loaded):  KRNL286 APP.EXE [arguments]
 */
#include "kernel.h"

static bootinfo bi;
static char app_path[80];

/* Set a protected-mode interrupt vector; returns the previous one. */
static u32 hook_int(u8 n, void (__far *h)(void))
{
    regs16 r;
    u32 old;
    r.ax = 0x0204;
    r.bx = n;
    r.ds = r.es = 0;
    dpmi31(&r);
    old = ((u32)r.cx << 16) | r.dx;
    r.ax = 0x0205;
    r.bx = n;
    r.cx = FP_SEG(h);
    r.dx = FP_OFF(h);
    dpmi31(&r);
    return old;
}

/* Split the PSP command tail into the program path and the rest; the rest
   becomes the command tail the application sees. */
static int parse_command_line(void)
{
    u8 FAR *psp = MK_FP(psp_sel, 0);
    u8 len = psp[0x80];
    u16 i = 0, n = 0, j, dot = 0;
    char rest[127];

    if (len > 126)
        len = 126;
    while (i < len && (psp[0x81 + i] == ' ' || psp[0x81 + i] == '\t'))
        i++;
    while (i < len && psp[0x81 + i] != ' ' && psp[0x81 + i] != '\t' &&
           n < sizeof app_path - 5) {
        char c = (char)psp[0x81 + i++];
        if (c == '.')
            dot = 1;
        else if (c == '\\' || c == '/' || c == ':')
            dot = 0;
        app_path[n++] = c;
    }
    if (!n)
        return 0;
    if (!dot) {
        app_path[n++] = '.';
        app_path[n++] = 'E';
        app_path[n++] = 'X';
        app_path[n++] = 'E';
    }
    app_path[n] = 0;

    for (j = 0; i < len; i++)
        rest[j++] = (char)psp[0x81 + i];
    psp[0x80] = (u8)j;
    for (i = 0; i < j; i++)
        psp[0x81 + i] = (u8)rest[i];
    psp[0x81 + j] = '\r';
    return 1;
}

void __cdecl kmain(bootinfo FAR *pbi)
{
    regs16 r;
    module FAR *app;
    u16 rc;

    kmemcpy(&bi, pbi, sizeof bi);
    if (bi.magic != BOOTINFO_MAGIC) {
        kputs("w16: bad boot information\r\n");
        kexit_to_dos(0xFF);
    }
    cpl = bi.cpl;
    psp_sel = bi.psp_sel;
    r.ax = 0x0003;
    r.ds = r.es = 0;
    dpmi31(&r);
    sel_inc = r.ax;
    for (sel_shift = 0; (1u << sel_shift) < sel_inc; sel_shift++)
        ;

    global_init();
    thunk_init();
    module_register_kernel(&bi);
    old21 = hook_int(0x21, int21_handler);
    hook_int(0x3D, int3d_handler);      /* floating-point fixups (FWAIT) */

    if (!parse_command_line()) {
        kputs("usage: KRNL286 program.exe [arguments]\r\n");
        kexit_to_dos(1);
    }
    app = module_load_program(app_path, &rc);
    if (!app) {
        kputs("w16: cannot load ");
        kputs(app_path);
        kputs(", error ");
        kputdec(rc);
        kputs("\r\n");
        kexit_to_dos(0xFE);
    }
    /* the first task: the rest of KERNEL's command line is its tail;
       nCmdShow 1 (observed for the shell) */
    {
        static const u16 show[2] = { 2, 1 };
        u8 tail[0x80];
        u8 FAR *psp = MK_FP(psp_sel, 0);
        u16 i, n = psp[0x80] < 0x7E ? psp[0x80] : 0x7E;
        u16 ad = NE_HDR(app)->auto_data;
        tail[0] = (u8)n;
        for (i = 0; i < n; i++)
            tail[1 + i] = psp[0x81 + i];
        tail[1 + n] = '\r';
        if (!ad || ad > app->seg_count ||
            !task_create(app, app->seg[ad - 1].sel, app->seg[ad - 1].hnd, 0,
                         (const u8 FAR *)tail, (const u16 FAR *)show, 0)) {
            kputs("w16: not a runnable application\r\n");
            kexit_to_dos(0xFE);
        }
    }
    rc = task_boot();
    kexit_to_dos((u8)rc);
}
