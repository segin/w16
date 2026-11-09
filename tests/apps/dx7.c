/*
 * dx7.c - DX7.EXE: a DOS program started from Standard-mode Windows.
 * Run as the shell (tests/dosapp.sh, or tests/dropin.sh -r -f
 * build/dos/DX7.EXE=DX7.EXE -f build/dos/DXD.COM=DXD.COM -i
 * 'boot:shell=C:\DX7.EXE'), it starts C:\DXD.COM (tests/dos/dxd.asm)
 * with WinExec, so WINOLDAP and the task switcher (WSWAP) swap Windows
 * out and run it, then pumps messages until DXD has written C:\DXD.OK
 * (or about 60 seconds pass), logs what happened and ends, which ends
 * Windows. To compare how a DOS program runs under Microsoft's DOSX and
 * w16's (ARCHITECTURE.md 9.6). Command tail: a digit n starts DXD n
 * times in a row.
 */
#include <windows.h>

#define TK_REF "dx6ref.h"
#include "tklog.h"

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl get_0040h(void);

static int ok_exists(void)
{
    OFSTRUCT of;
    return OpenFile("C:\\DXD.OK", &of, OF_EXIST) != HFILE_ERROR;
}

int __cdecl tmain(void)
{
    WORD hinst = init_regs[5];
    volatile DWORD FAR *ticks = (volatile DWORD FAR *)MAKELP(get_0040h(), 0x6C);
    LPSTR tail = (LPSTR)MAKELP(init_regs[6], init_regs[1]);
    WORD n, times = 1, run;
    MSG msg;
    OFSTRUCT of;

    for (n = 0; n < 8 && tail[n] != '\r' && tail[n]; n++)
        if (tail[n] >= '1' && tail[n] <= '9') {
            times = tail[n] - '0';
            break;
        }
    out("DX7 start\r\n");
    obs("InitApp", (WORD)U_INITAPP(hinst));
    for (run = 0; run < times; run++) {
        DWORD t0;
        UINT r;
        OpenFile("C:\\DXD.OK", &of, OF_DELETE);
        r = WinExec("C:\\DXD.COM", SW_SHOWNORMAL);
        obs("WinExec", r);
        /* WINOLDAP's window may come up iconic; the program runs when
           it is opened */
        t0 = *ticks;
        while (*ticks - t0 < 18) {
            while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            Yield();
        }
        {
            HWND w = GetActiveWindow();
            obs_env("active window", (WORD)w);
            obs("active window iconic", w ? IsIconic(w) : 0xFFFF);
            if (w && IsIconic(w))
                OpenIcon(w);
        }
        t0 = *ticks;
        while (!ok_exists() && *ticks - t0 < 1092) {
            while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            Yield();
        }
        obs("DXD finished", ok_exists());
        obs_env("ticks waited", (WORD)(*ticks - t0));
        /* a little more, for WINOLDAP to finish */
        t0 = *ticks;
        while (*ticks - t0 < 36) {
            while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            Yield();
        }
    }
    out("DX7 done\r\n");
    return 0;
}
