/*
 * tk57.c - what KERNEL does differently in 386 Enhanced mode (KRNL386
 * under WIN386) and what it asks of the VxDs, black-box. Run as the shell
 * (tests/refrun.sh TK57.EXE /3 and /S; tests/dropin.sh -3 for w16's
 * KRNL386, with -c 'WIN /3 /B' for Enhanced mode):
 *  - GetWinFlags and INT 2Fh 1600h in each mode;
 *  - the Reboot device's protected-mode API (INT 2Fh 1684h BX=0009h;
 *    Ralf Brown's Interrupt List 61): function 0202h gives back what
 *    KERNEL set with 0201h at its start (ES:DI its Ctrl+Alt+Del procedure,
 *    DS:SI the byte that tells a hung application from a running one):
 *    whether those are KERNEL's, and the byte's value;
 *  - with the tail "mod": the module a DOS program (COMMAND.COM, left
 *    running) runs under, by name and file;
 *  - with the tail "woa": which module KERNEL starts for a DOS program
 *    (WinExec of COMMAND.COM): TK33.EXE copied over WINOLDAP.MOD and
 *    WINOA386.MOD logs its own file name.
 * Names start with the mode ("S " Standard, "E " Enhanced), so that the
 * reference table (tk57ref.h, tools/obsref.py from runs in both modes)
 * keeps each mode's values.
 */
#include <windows.h>

#define TK_REF "tk57ref.h"
#include "tklog.h"

extern void __cdecl __far k57_int2f(WORD __far *regs);
extern void __cdecl __far k57_call(void __far *entry, WORD __far *regs);

extern HANDLE FAR PASCAL GetExePtr(HANDLE h);       /* KERNEL.133 */

WORD lar(WORD s);
#pragma aux lar = "lar ax, ax" "jz ok" "xor ax, ax" "ok:" parm [ax] value [ax];

static char nm[64];
static int enh;

static const char *n(const char *s)
{
    int i = 0;
    nm[i++] = enh ? 'E' : 'S';
    nm[i++] = ' ';
    while (*s && i < (int)sizeof nm - 1)
        nm[i++] = *s++;
    nm[i] = 0;
    return nm;
}

static void clr(WORD *r)
{
    int i;
    for (i = 0; i < 9; i++)
        r[i] = 0;
}

/* the selector of KERNEL's data segment: THHOOK's (KERNEL.332) */
static WORD kernel_ds(void)
{
    FARPROC p = GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(332));
    return SELECTOROF(p);
}

static void reboot_api(void)
{
    WORD r[9];
    void __far *e;
    HMODULE hk = GetModuleHandle("KERNEL");
    clr(r);
    r[0] = 0x1684;
    r[1] = 0x0009;
    k57_int2f(r);
    e = MAKELP(r[6], r[5]);
    obs(n("Reboot API entry"), e != 0);
    if (!e)
        return;
    clr(r);
    r[0] = 0x0202;
    k57_call(e, r);
    obs(n("Reboot 0202h carry"), r[7] & 1);
    obs(n("Reboot 0202h AX"), r[0]);
    obs(n("Reboot 0202h ES:DI set"), r[6] != 0);
    obs(n("Reboot 0202h ES is KERNEL's"), r[6] && GetExePtr((HANDLE)r[6]) == (HANDLE)hk);
    obs(n("Reboot 0202h ES code"), r[6] && (lar(r[6]) & 0x0800) != 0);
    obs_env(n("Reboot 0202h ES"), r[6]);
    obs_env(n("Reboot 0202h DI"), r[5]);
    obs(n("Reboot 0202h DS==KERNEL DS"), r[8] == kernel_ds());
    obs_env(n("Reboot 0202h DS"), r[8]);
    obs_env(n("Reboot 0202h SI"), r[4]);
    obs_env(n("Reboot 0202h BX"), r[1]);
    obs_env(n("Reboot 0202h CX"), r[2]);
    obs_env(n("Reboot 0202h DX"), r[3]);
    if (r[8] == kernel_ds())
        obs(n("Reboot 0202h byte at DS:SI"), *(BYTE FAR *)MAKELP(r[8], r[4]));
}

int __cdecl tmain(void)
{
    LPSTR tail = (LPSTR)MAKELP(init_regs[6], init_regs[1]);   /* ES:BX */
    WORD r[9];
    DWORD f = GetWinFlags();
    int i;
    UINT h;
    char buf[80];
    enh = (f & WF_ENHANCED) != 0;
    out("TK57 start\r\n");
    obs(n("GetWinFlags"), (WORD)f);
    obs(n("GetWinFlags high"), HIWORD(f));
    clr(r);
    r[0] = 0x1600;
    k57_int2f(r);
    obs(n("INT 2Fh 1600h AX"), r[0]);
    clr(r);
    r[0] = 0x1683;
    k57_int2f(r);
    obs(n("INT 2Fh 1683h BX"), r[1]);
    if (enh)
        reboot_api();
    if (tail[0] == 'w') {
        /* TK33 in WINOLDAP.MOD's and WINOA386.MOD's place logs its file */
        out("WinExec COMMAND.COM\r\n");
        h = WinExec("COMMAND.COM /C EXIT", SW_SHOWNORMAL);
        obs("WinExec > 32", h > 32);
        obs_env("WinExec", h);
        GetModuleFileName(GetExePtr((HANDLE)h), buf, sizeof buf);
        obs_str("WinExec module file", buf);
        for (i = 0; i < 200; i++)
            Yield();
    }
    if (tail[0] == 'm') {
        /* which module a DOS program runs under: COMMAND.COM stays, its
           WINOLDAP-type module with it */
        HMODULE m;
        out("WinExec COMMAND.COM (stays)\r\n");
        h = WinExec("COMMAND.COM", SW_SHOWMINIMIZED);
        obs(n("stays: WinExec > 32"), h > 32);
        obs_env("stays: WinExec", h);
        for (i = 0; i < 400; i++)
            Yield();
        m = GetModuleHandle("WINOLDAP");
        obs(n("module WINOLDAP loaded"), m != 0);
        buf[0] = 0;
        if (m)
            GetModuleFileName(m, buf, sizeof buf);
        obs_str("module WINOLDAP file", buf);
        m = GetModuleHandle("WINOA386");
        obs(n("module WINOA386 loaded"), m != 0);
        buf[0] = 0;
        if (m)
            GetModuleFileName(m, buf, sizeof buf);
        obs_str("module WINOA386 file", buf);
        m = GetModuleHandle("GRABBER");
        obs(n("module GRABBER loaded"), m != 0);
        buf[0] = 0;
        if (m)
            GetModuleFileName(m, buf, sizeof buf);
        obs_str("module GRABBER file", buf);
        obs(n("module GRABBER usage"), m ? GetModuleUsage(m) : 0);
    }
    out("TK57 done\r\n");
    return 0;
}
