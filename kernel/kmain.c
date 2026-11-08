/*
 * kmain.c - KERNEL initialisation (M1).
 *
 * Usage (from DOS, with a DPMI host loaded):  KRNL286 APP.EXE [arguments]
 */
#include "kernel.h"

static bootinfo bi;
static char app_path[80];

static void hook_int21(void)
{
    regs16 r;
    void (__far *h)(void) = int21_handler;
    r.ax = 0x0204;
    r.bx = 0x21;
    r.ds = r.es = 0;
    dpmi31(&r);
    old21 = ((u32)r.cx << 16) | r.dx;
    r.ax = 0x0205;
    r.bx = 0x21;
    r.cx = FP_SEG(h);
    r.dx = FP_OFF(h);
    dpmi31(&r);
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
    hook_int21();

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
    rc = task_start(app, NULL);
    kexit_to_dos((u8)rc);
}
