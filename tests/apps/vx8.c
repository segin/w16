/*
 * vx8.c - VX8.EXE: how Windows 3.1's Reboot device uses KERNEL's
 * Ctrl+Alt+Del handler and the sanity-check byte (ARCHITECTURE.md 12.8,
 * the handover note's step 5). Started by WIN.INI run= in an input run
 * (tests/scripts/input/vx8*.txt press Ctrl+Alt+Del). [vx8] mode= in
 * WIN.INI:
 *   bit 0  replace KERNEL's handler (Reboot PM API AX=0201h, as KERNEL
 *          does: ES:DI the handler, DS:SI the sanity byte) with VX8's own,
 *          which records each call (registers, stack: tests/apps/vx8s.asm)
 *          and jumps on to KERNEL's; the sanity byte is then VX8's own
 *          (5Ah at first): its value is logged whenever it changes;
 *   bit 1  stop responding: a loop that never yields (it still logs).
 * Otherwise VX8 runs a message loop (100 ms timer) for 60 s, logging.
 * Logs to TK.LOG: KERNEL's handler (0202h), the 0201h result, then the
 * changes.
 */
#include <windows.h>

#define TK_REF "vx3ref.h"
#include "tklog.h"

extern void __cdecl __far vx_int2f(WORD __far *regs);
extern void __cdecl __far vx_call(void __far *entry, WORD __far *regs);
extern void __cdecl __far vx8_setup(WORD __far *buf, void __far *kernel, WORD kds);
extern void __cdecl __far vx8_handler(void);

static WORD rec[1 + 8 * 24 + 8] = { 0 };      /* then the log's name (vx8s.asm) */
static BYTE __near sanity = 0x5A;
static WORD logged;
static BYTE last_sanity = 0x5A;

static void __far *entry(WORD id)
{
    WORD r[8];
    int i;
    for (i = 0; i < 8; i++) r[i] = 0;
    r[0] = 0x1684;
    r[1] = id;
    vx_int2f(r);
    out("OBS ~entry_"); outhex(id); out("="); outhex(r[6]); out(":"); outhex(r[5]); out("\r\n");
    if (!r[5] && !r[6]) return NULL;
    return (void __far *)MAKELONG(r[5], r[6]);
}

static void show(const char *name, WORD *r)
{
    out("OBS ~"); out(name); out("=ax:"); outhex(r[0]);
    out(" bx:"); outhex(r[1]); out(" cx:"); outhex(r[2]); out(" dx:"); outhex(r[3]);
    out(" si:"); outhex(r[4]); out(" di:"); outhex(r[5]); out(" es:"); outhex(r[6]);
    out(" cf:"); outhex(r[7] & 1); out("\r\n");
}

static void changes(void)
{
    static const char *names[12] = { "ax", "bx", "cx", "dx", "si", "di", "bp", "ds", "es", "ss", "sp", "fl" };
    int i;
    while (logged < rec[0]) {
        WORD *p = rec + 1 + logged * 24;
        out("OBS ~call");
        outhex(logged);
        out("=");
        for (i = 0; i < 12; i++) { out(names[i]); out(":"); outhex(p[i]); out(" "); }
        out("stack:");
        for (i = 12; i < 20; i++) { out(" "); outhex(p[i]); }
        out(" tick:"); outhex(GetTickCount() & 0xFFFF);
        out("\r\n");
        logged++;
    }
    if (sanity != last_sanity) {
        out("OBS ~sanity="); outhex(sanity); out(" tick:"); outhex(GetTickCount() & 0xFFFF); out("\r\n");
        last_sanity = sanity;
    }
}

int __cdecl tmain(void)
{
    void __far *reboot, __far *kern;
    WORD r[8];
    int i, n, mode = GetProfileInt("vx8", "mode", 1);
    MSG m;
    UINT id;

    out("VX8 Ctrl+Alt+Del handler probe, mode "); outhex(mode); out("\r\n");
    reboot = entry(0x0009);
    if (!reboot) return 1;
    for (i = 0; i < 8; i++) r[i] = 0;
    r[0] = 0x0202;
    vx_call(reboot, r);
    show("get_0202", r);
    kern = (void __far *)MAKELONG(r[5], r[6]);
    if (mode & 1) {
        HINSTANCE k = LoadLibrary("KERNEL");       /* a DLL's instance: its data segment */
        out("OBS ~kernel_ds="); outhex((WORD)k); out("\r\n");
        lstrcpy((LPSTR)(rec + 1 + 8 * 24), "C:\\VX8H.LOG");
        vx8_setup(rec, kern, (WORD)k);
        for (i = 0; i < 8; i++) r[i] = 0;
        r[0] = 0x0201;
        r[4] = (WORD)(&sanity);         /* DS:SI: DS is DGROUP */
        r[5] = LOWORD((DWORD)(void __far *)vx8_handler);
        r[6] = HIWORD((DWORD)(void __far *)vx8_handler);
        vx_call(reboot, r);
        show("set_0201", r);
    }
    if (mode & 2) {
        out("VX8 hanging\r\n");
        for (;;) changes();
    }
    id = SetTimer(0, 0, 100, NULL);
    for (n = 0; n < 600 && GetMessage(&m, 0, 0, 0); ) {
        if (m.message == WM_TIMER && m.wParam == id) { n++; changes(); }
        DispatchMessage(&m);
    }
    KillTimer(0, id);
    changes();
    out("VX8 done\r\n");
    return 0;
}
