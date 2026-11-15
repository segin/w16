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
 *   bit 1  stop responding: a loop that never yields (it still logs);
 *   bit 2  VX8's handler sets its sanity byte to 1 (as KERNEL's does);
 *   bit 3  VX8's handler returns (IRET) instead of going on to KERNEL's;
 *   bit 4  the 100 ms timer sets the byte again when found zeroed (as
 *          KERNEL does at task switches), logging when;
 *   bit 5  the handler also appends each record to C:\VX8H.LOG (INT 21h);
 *   bit 6  watch KERNEL's data segment (data only) every 100 ms: each byte
 *          that goes from 1 to 0 or 0 to 1 is logged with the BIOS tick
 *          (finds KERNEL's sanity byte, when the Reboot device zeroes it
 *          and when KERNEL sets it again);
 *   bit 7  no Reboot 0202h call at the start;
 * each handler record has the BIOS tick (0040:006Ch) and the byte's value
 * at entry.
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
extern void __cdecl __far vx8_setflags(WORD flags, BYTE __far *sane);

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
        out(" bios:"); outhex(p[20]); out(" sane_in:"); outhex(p[22]);
        out(" tick:"); outhex(GetTickCount() & 0xFFFF);
        out("\r\n");
        logged++;
    }
    if (sanity != last_sanity) {
        out("OBS ~sanity="); outhex(sanity); out(" tick:"); outhex(GetTickCount() & 0xFFFF); out("\r\n");
        last_sanity = sanity;
    }
}

static BYTE __far *kcopy;
static WORD klen, kds_sel, nwatch;

static void watch(void)
{
    BYTE __far *k = (BYTE __far *)MAKELP(kds_sel, 0);
    WORD i;
    for (i = 0; i < klen; i++) {
        BYTE a = kcopy[i], b = k[i];
        if (a != b) {
            if (((a == 1 && b == 0) || (a == 0 && b == 1)) && nwatch < 300) {
                nwatch++;
                out("OBS ~kds@"); outhex(i); out("="); outhex(b);
                out(" bios:"); outhex(*(WORD __far *)MAKELP(0x40, 0x6C)); out("\r\n");
            }
            kcopy[i] = b;
        }
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
    if (!(mode & 128)) {        /* bit 7: no 0202h call */
        r[0] = 0x0202;
        vx_call(reboot, r);
        show("get_0202", r);
    }
    kern = (void __far *)MAKELONG(r[5], r[6]);
    if (mode & 1) {
        HINSTANCE k = LoadLibrary("KERNEL");       /* a DLL's instance: its data segment */
        out("OBS ~kernel_ds="); outhex((WORD)k); out("\r\n");
        lstrcpy((LPSTR)(rec + 1 + 8 * 24), "C:\\VX8H.LOG");
        /* the handler's code segment kept in memory: called at interrupt
         * time, a discarded segment's fault there is fatal */
        LockSegment((UINT)SELECTOROF((void __far *)vx8_handler));
        LockData(0);
        vx8_setup(rec, kern, (WORD)k);
        for (i = 0; i < 8; i++) r[i] = 0;
        r[0] = 0x0201;
        r[4] = (WORD)(&sanity);         /* DS:SI: DS is DGROUP */
        r[5] = LOWORD((DWORD)(void __far *)vx8_handler);
        r[6] = HIWORD((DWORD)(void __far *)vx8_handler);
        vx_call(reboot, r);
        show("set_0201", r);
        vx8_setflags(((mode & 4) ? 2 : 0) | ((mode & 8) ? 4 : 0) | ((mode & 32) ? 1 : 0), (BYTE __far *)&sanity);
    }
    if (mode & 64) {
        DWORD lim;
        kds_sel = (WORD)LoadLibrary("KERNEL");
        lim = GetSelectorLimit(kds_sel);
        klen = lim > 0x7FFF ? 0x8000 : (WORD)lim + 1;
        kcopy = (BYTE __far *)GlobalLock(GlobalAlloc(GMEM_FIXED, klen));
        for (i = 0; i < (int)klen; i++) kcopy[i] = ((BYTE __far *)MAKELP(kds_sel, 0))[i];
        out("OBS ~kds="); outhex(kds_sel); out(" len:"); outhex(klen); out("\r\n");
    }
    if (mode & 2) {
        out("VX8 hanging\r\n");
        for (;;) changes();
    }
    id = SetTimer(0, 0, 100, NULL);
    for (n = 0; n < 600 && GetMessage(&m, 0, 0, 0); ) {
        if (m.message == WM_TIMER && m.wParam == id) {
            n++;
            if (mode & 64) watch();
            if ((mode & 16) && !sanity) {
                /* zeroed by the Reboot device: noted, set again as KERNEL
                 * does at its task switches */
                out("OBS ~zeroed bios:"); outhex(*(WORD __far *)MAKELP(0x40, 0x6C));
                out(" tick:"); outhex(GetTickCount() & 0xFFFF); out("\r\n");
                sanity = 1; last_sanity = 1;
            }
            changes();
        }
        DispatchMessage(&m);
    }
    KillTimer(0, id);
    changes();
    out("VX8 done\r\n");
    return 0;
}
