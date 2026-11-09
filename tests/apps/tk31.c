/*
 * tk31.c - which registers KERNEL's exports keep. Each export is called
 * (tk31s.asm) with known values in BX, CX, SI, DI, BP, ES = DS and the
 * carry set; the probe logs a mask of what came back unchanged:
 *   01 BX  02 CX  04 SI  08 DI  10 BP  20 ES  40 carry  80 DS
 * (DX:AX is the result). Windows 3.1's own code relies on some of these
 * (TK24: GetTaskQueueDS/ES, GetDOSEnvironment); w16's KERNEL is mostly C,
 * which keeps only SI, DI, BP and DS. Run as the shell (tests/dropin.sh,
 * and -r for Windows 3.1) and compare.
 */
#include <windows.h>

#define TK_REF "tk31ref.h"
#include "tklog.h"

extern WORD __cdecl w_regs[10];
extern void __cdecl __far w_call(FARPROC fn, int nargs, WORD a0, WORD a1,
                                  WORD a2, WORD a3, WORD a4, WORD a5);

static char buf[128];
static char str[] = "USER";
static char shl[] = "SHELL.DLL";
static char one[] = "#1";
static char sec[] = "windows";
static char key[] = "Beep";

static void call(const char *name, WORD ord, int n, WORD a0, WORD a1, WORD a2,
                 WORD a3, WORD a4, WORD a5)
{
    FARPROC f = GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(ord));
    WORD m = 0, ds = SELECTOROF((void FAR *)buf);
    char n2[48];
    if (!f) {
        obs(name, 0xFFFF);
        return;
    }
    w_call(f, n, a0, a1, a2, a3, a4, a5);
    if (w_regs[1] == 0x2222) m |= 0x01;
    if (w_regs[2] == 0x3333) m |= 0x02;
    if (w_regs[4] == 0x5555) m |= 0x04;
    if (w_regs[5] == 0x6666) m |= 0x08;
    if (w_regs[6] == 0x7777) m |= 0x10;
    if (w_regs[8] == ds) m |= 0x20;
    if (w_regs[9] & 1) m |= 0x40;
    if (w_regs[7] == ds) m |= 0x80;
    lstrcpy(n2, name);
    lstrcat(n2, " kept");
    obs(n2, m);
}

#define SEG(p) SELECTOROF((void FAR *)(p))
#define OFF(p) OFFSETOF((void FAR *)(p))

int __cdecl tmain(void)
{
    WORD user = GetModuleHandle("USER"), h, ds = SEG(buf);
    out("TK31 start\r\n");
    call("GetVersion(3)", 3, 0, 0, 0, 0, 0, 0, 0);
    call("GetCurrentTask(36)", 36, 0, 0, 0, 0, 0, 0, 0);
    call("GetCurrentPDB(37)", 37, 0, 0, 0, 0, 0, 0, 0);
    call("GetModuleHandle(47)", 47, 2, ds, OFF(str), 0, 0, 0, 0);
    call("GetModuleUsage(48)", 48, 1, user, 0, 0, 0, 0, 0);
    call("GetModuleFileName(49)", 49, 4, user, ds, OFF(buf), 128, 0, 0);
    call("GetProcAddress(50)", 50, 3, user, ds, OFF(one), 0, 0, 0);
    call("GetProfileInt(57)", 57, 5, ds, OFF(sec), ds, OFF(key), 7, 0);
    call("GlobalAlloc(15)", 15, 3, GMEM_MOVEABLE, 0, 64, 0, 0, 0);
    h = w_regs[0];
    call("GlobalLock(18)", 18, 1, h, 0, 0, 0, 0, 0);
    call("GlobalUnlock(19)", 19, 1, h, 0, 0, 0, 0, 0);
    call("GlobalSize(20)", 20, 1, h, 0, 0, 0, 0, 0);
    call("GlobalHandle(21)", 21, 1, h | 1, 0, 0, 0, 0, 0);
    call("GlobalFlags(22)", 22, 1, h, 0, 0, 0, 0, 0);
    call("GlobalReAlloc(16)", 16, 4, h, 0, 128, 0, 0, 0);
    call("GlobalFree(17)", 17, 1, h, 0, 0, 0, 0, 0);
    call("LockSegment(23)", 23, 1, 0xFFFF, 0, 0, 0, 0, 0);
    call("UnlockSegment(24)", 24, 1, 0xFFFF, 0, 0, 0, 0, 0);
    call("GlobalCompact(25)", 25, 2, 0, 0, 0, 0, 0, 0);
    call("lstrcpy(88)", 88, 4, ds, OFF(buf), ds, OFF(str), 0, 0);
    call("lstrcat(89)", 89, 4, ds, OFF(buf), ds, OFF(str), 0, 0);
    call("lstrlen(90)", 90, 2, ds, OFF(buf), 0, 0, 0, 0);
    call("LoadLibrary(95)", 95, 2, ds, OFF(shl), 0, 0, 0, 0);
    h = w_regs[0];
    call("FreeLibrary(96)", 96, 1, h, 0, 0, 0, 0, 0);
    call("GetExeVersion(105)", 105, 0, 0, 0, 0, 0, 0, 0);
    call("SetErrorMode(107)", 107, 1, 0, 0, 0, 0, 0, 0);
    call("GetWinFlags(132)", 132, 0, 0, 0, 0, 0, 0, 0);
    call("GetWindowsDirectory(134)", 134, 3, ds, OFF(buf), 128, 0, 0, 0);
    call("GetSystemDirectory(135)", 135, 3, ds, OFF(buf), 128, 0, 0, 0);
    call("GetDriveType(136)", 136, 1, 2, 0, 0, 0, 0, 0);
    call("GetNumTasks(152)", 152, 0, 0, 0, 0, 0, 0, 0);
    call("GetTaskDS(155)", 155, 0, 0, 0, 0, 0, 0, 0);
    call("GetExpWinVer(167)", 167, 1, user, 0, 0, 0, 0, 0);
    call("GetFreeSpace(169)", 169, 1, 0, 0, 0, 0, 0, 0);
    call("GetSelectorBase(186)", 186, 1, ds, 0, 0, 0, 0, 0);
    call("SetHandleCount(199)", 199, 1, 20, 0, 0, 0, 0, 0);
    call("IsBadReadPtr(334)", 334, 3, ds, OFF(buf), 4, 0, 0, 0);
    call("GetHeapSpaces(138)", 138, 1, user, 0, 0, 0, 0, 0);
    call("GetAppCompatFlags(354)", 354, 1, 0, 0, 0, 0, 0, 0);
    out("TK31 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
