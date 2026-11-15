/*
 * tk44.c - where keyboard interrupts go after the boot (the w16 DOSX
 * input bug): started from WIN.INI load= under tests/inputrun.sh, it
 * chains a counter onto the protected-mode INT 09h and INT 08h handlers
 * (INT 31h 0204h/0205h) and, every two seconds for 90 seconds, logs the
 * counts, the real-mode vectors of INT 08h and 09h (INT 31h 0200h), and
 * the BIOS keyboard buffer's head and tail (0040:001A/001C: keys a
 * real-mode INT 09h handler of the BIOS has stored).
 * Compare tests/inputrun.sh -r -X build/dos/DOSX.EXE with -r.
 */
#include <windows.h>


static int TK44_HOOK = 0;      /* 1: count INT 08h/09h (tk44s.asm) */
#define TK_REF "tk44ref.h"
#include "tklog.h"

extern WORD __cdecl n08, n09;
extern void __cdecl __far hook_install(void);

static void int28(void);
#pragma aux int28 = "mov ax, 0001h" "int 28h" modify [ax];
static DWORD rmvec(BYTE n);
#pragma aux rmvec = "mov ax, 0200h" "int 31h" "mov ax, dx" "mov dx, cx" \
    parm [bl] value [dx ax] modify [cx];

int __cdecl tmain(void)
{
    WORD i, sel40 = (WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0040H");
    WORD FAR *bda = (WORD FAR *)MAKELP(sel40, 0);
    WORD FAR *ivt = (WORD FAR *)MAKELP((WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0000H"), 0);
    MSG m;
    int busy;
    out("TK44 start\r\n");
    if (TK44_HOOK)
        hook_install();
    /* the command tail "b": wait busily (PeekMessage); else with
       GetMessage and a timer, so that the system goes idle */
    {
        LPSTR tail = (LPSTR)MAKELP(init_regs[6], init_regs[1]);
        busy = tail[0] == 'b' || tail[1] == 'b';
        if (tail[0] == 'r' || tail[1] == 'r')
            busy = 2;
        if (!busy && tail[0] != 'i' && tail[1] != 'i')
            busy = 2;                   /* default: INT 28h; "i": GetMessage */
    }
    if (!busy)
        SetTimer(0, 1, 2000, NULL);
    for (i = 0; i < 45; i++) {
        if (busy == 2) {
            /* INT 28h (reflected to real mode) over and over: hardware
               interrupts then arrive in real mode */
            DWORD end = GetTickCount() + 2000;
            while (GetTickCount() < end)
                int28();
        } else if (busy) {
            DWORD end = GetTickCount() + 2000;
            while (GetTickCount() < end)
                if (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
                    DispatchMessage(&m);
        } else {
            while (GetMessage(&m, 0, 0, 0) && m.message != WM_TIMER)
                DispatchMessage(&m);
        }
        obs("t", i);
        obs("  pm INT 08h calls", n08);
        obs("  pm INT 09h calls", n09);
        obs("  rm INT 08h seg", HIWORD(rmvec(8)));
        obs("  rm INT 09h seg", HIWORD(rmvec(9)));
        obs("  rm INT 09h off", LOWORD(rmvec(9)));
        obs("  BIOS kbd head", bda[0x1A / 2]);
        obs("  BIOS kbd tail", bda[0x1C / 2]);
        obs("  IVT 08h seg", ivt[0x08 * 2 + 1]);
        obs("  IVT 09h seg", ivt[0x09 * 2 + 1]);
        obs("  IVT 09h off", ivt[0x09 * 2]);
        obs("  IVT 74h seg", ivt[0x74 * 2 + 1]);
    }
    out("TK44 done\r\n");
    return 0;
}
