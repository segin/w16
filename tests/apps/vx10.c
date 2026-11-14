/*
 * vx10.c - VX10.EXE: what a DOS box's Windows side (WINOLDAP, the grabber)
 * asks of the VMM and the virtual devices in 386 Enhanced mode (the WIN386
 * handover note, step 6). Started by WIN.INI run= before the MS-DOS
 * Prompt is opened (tests/vx10run.sh); it hooks INT 2Fh in protected mode
 * (DPMI 0204h/0205h, as Undocumented Windows' WISPY does) and records
 * every call but the idle ones (1680h, 1689h), and hands out its own
 * entry points for the VxD APIs (INT 2Fh 1684h) of the devices other than
 * VTD, Reboot and VTDAPI, which record each call around the real entry
 * (tests/apps/vx10s.asm). Black box: registers and the data the callers
 * pass at DS:SI and ES:DI only (ARCHITECTURE.md 3).
 * Each new record goes to TK.LOG every 100 ms:
 *   OBS ~i2f<n>=from:CS:IP ax:.. bx:.. cx:.. dx:.. si:.. di:.. ds:.. es:..
 *        -> ax:.. ... fl:.. t:<BIOS tick> dssi:<16 words> esdi:<16 words>
 *        esdi_out:<8 words>
 *   OBS ~api<n>=dev:<id> from:CS:IP ... the same
 * [vx10] secs= in WIN.INI: how long it records (default 300); hook=0: no
 * hook (a control run).
 */
#include <windows.h>

#define TK_REF "vx3ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern void __cdecl __far vx10_setup(WORD bufsel);
extern void __cdecl __far vx10_hook(int on);

static WORD __far *buf;
static WORD shown;

static void regs(const char *tag, WORD __far *p)
{
    static const char *names[8] = { "ax", "bx", "cx", "dx", "si", "di", "ds", "es" };
    int i;
    out(tag);
    for (i = 0; i < 8; i++) { out(names[i]); out(":"); outhex(p[i]); out(" "); }
}

static void words(const char *tag, WORD __far *p, int n)
{
    int i;
    out(tag);
    for (i = 0; i < n; i++) { out(" "); outhex(p[i]); }
}

static void flush(void)
{
    while (shown < buf[0]) {
        WORD __far *r = buf + 32 + shown * 64;
        out(r[0] == 1 ? "OBS ~i2f" : "OBS ~api");
        outhex(shown);
        out("=");
        if (r[0] == 2) { out("dev:"); outhex(r[1]); out(" "); }
        out("from:"); outhex(r[2]); out(":"); outhex(r[3]); out(" ");
        regs("", r + 4);
        regs("-> ", r + 12);
        out("fl:"); outhex(r[20]);
        out(" t:"); outhex(r[21]);
        words(" dssi:", r + 24, 16);
        words(" esdi:", r + 40, 16);
        words(" esdi_out:", r + 56, 8);
        out("\r\n");
        shown++;
    }
}

int __cdecl tmain(void)
{
    HGLOBAL h;
    MSG m;
    UINT id;
    int n, secs = GetProfileInt("vx10", "secs", 300);

    /* a whole application's start: started by WIN.INI run= without
     * InitApp, Program Manager then faulted in KRNL386 (0001:7D8F, under
     * Windows 3.1 too; VX8 met the same at Ctrl+Alt+Del) */
    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    out("VX10 DOS box API spy\r\n");
    h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 65536L);
    if (!h) return 1;
    buf = (WORD __far *)GlobalLock(h);
    GlobalPageLock((HGLOBAL)SELECTOROF(buf));
    /* the hook's code is called from other tasks: kept in memory */
    LockSegment((UINT)SELECTOROF((void __far *)vx10_hook));
    vx10_setup(SELECTOROF(buf));
    if (GetProfileInt("vx10", "hook", 1)) {
        vx10_hook(1);
        out("VX10 hooked\r\n");
    }
    id = SetTimer(0, 0, 100, NULL);
    for (n = 0; n < secs * 10 && GetMessage(&m, 0, 0, 0); ) {
        if (m.message == WM_TIMER && m.wParam == id) {
            n++;
            flush();
        }
        DispatchMessage(&m);
    }
    KillTimer(0, id);
    /* the entries handed out stay in use by their callers: VX10 keeps its
     * hook and stays (never returns while Windows runs) */
    flush();
    out("VX10 done\r\n");
    for (;;) {
        if (!GetMessage(&m, 0, 0, 0)) break;
        DispatchMessage(&m);
    }
    return 0;
}
