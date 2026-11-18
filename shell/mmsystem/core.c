/*
 * core.c - w16's MMSYSTEM.DLL: its DriverProc (it is a [boot] drivers=
 * driver), the version, the interrupt stack, and the mmTask functions
 * (ARCHITECTURE.md 23).
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 1 ch. 25 and
 * Vol. 2 (installable drivers, LoadModule, GetMessage); the mm* task
 * functions' arguments from Wine's MMSYSTEM of before 2002 (X11
 * licence); the rest observed on Windows 3.1 with tests/mmsystem/mmp.c
 * (docs/notes/mmsystem-handover.md): MMSYSTEM opens "timer" (and
 * "joystick") from its DRV_LOAD and answers 0 to the other messages;
 * mmsystemGetVersion is 0101h; mmTaskBlock dispatches window messages
 * until a task message comes and returns its number; mmTaskSignal posts
 * WM_USER; mmTaskCreate starts MMTASK.TSK, which calls the function with
 * the instance data and ends when it returns; mmTaskCreate returns at
 * once, with the task's handle, before the function runs.
 */
#include "mmsys.h"

BOOL MMAPI SNDPLAYSOUND(LPCSTR name, UINT flags);    /* snd.c */

HINSTANCE __cdecl mm_hinst;               /* set by mm_libentry (mmfix.asm) */
static HDRVR joydrv;

int __cdecl __far mm_init(void)
{
    return 1;
}

/* The interrupt stack: 600h bytes, made at task time by the first
   timeSetEvent (3.1's: GlobalAlloc(GMEM_SHARE, 600h), a fixed block
   since a DLL asks: docs/notes/quicktime-handover.md) */
void __cdecl __far __loadds mm_makestack(void)
{
    HGLOBAL h;
    if (mm_istack)
        return;
    h = GlobalAlloc(GMEM_SHARE, 0x600);
    if (h)
        mm_istack = SELECTOROF(GlobalLock(h));
}

static void load(void)
{
    HDRVR h = OpenDriver("timer", NULL, 0);
    if (h) {
        FARPROC p = GetProcAddress(GetDriverModuleHandle(h), "DriverProc");
        if (p) {
            mm_tproc = p;
            mm_tdrv = h;
        } else
            CloseDriver(h, 0, 0);
    }
    joydrv = OpenDriver("joystick", NULL, 0);
}

LRESULT MMAPI MMDRIVERPROC(DWORD id, HDRVR h, UINT msg, LPARAM l1, LPARAM l2)
{
    (void)id;
    (void)h;
    (void)l1;
    (void)l2;
    switch (msg) {
    case DRV_LOAD:
        load();
        /* layer 3: the device drivers, MMSYSTEM's window, the start-up
           sound (3.1 plays SystemStart itself after opening the drivers;
           UNVERIFIED: whether SystemDefault would stand in for it) */
        dev_load();
        snd_init();
        SNDPLAYSOUND("SystemStart", 0x0001 | 0x0002);
        return 1;
    case DRV_ENABLE:
    case DRV_DISABLE:
    case DRV_FREE:
    case DRV_OPEN:
    case DRV_CLOSE:
    case DRV_INSTALL:                   /* 1 on 3.1 (MMP, drivers) */
    case DRV_EXITAPPLICATION:           /* 1 on 3.1 */
        return 1;
    }
    return 0;
}

UINT MMAPI MMSYSTEMGETVERSION(void)
{
    return 0x0101;
}

/* ---- tasks ---- */

/* MMTASK.TSK's data (shell/mmsystem/mmtask.asm), after the 16 bytes
   KERNEL keeps at the start of a task's data segment: the function and
   its argument, which mmTaskCreate puts there before the task runs (where
   3.1's are: MMP's task section reads them there) */
struct mmtaskdata {
    BYTE kernel[16];
    FARPROC fn;
    DWORD inst;
};

/* KERNEL's GetCurrentTask (KERNEL.36) gives the head of the task list in
   DX; each task database starts with the next one's handle and has its
   instance at 1Ch (Schulman et al., Undocumented Windows, ch. 5; w16's
   kernel/task.c keeps the layout) */
extern DWORD FAR PASCAL K_GETCURRENTTASK(void);

static WORD taskof(HINSTANCE hi)
{
    WORD t = HIWORD(K_GETCURRENTTASK());
    int n;
    for (n = 0; t && n < 200; n++) {
        if (*(WORD FAR *)MAKELP(t, 0x1C) == (WORD)hi)
            return t;
        t = *(WORD FAR *)MAKELP(t, 0);
    }
    return 0;
}

UINT MMAPI MMTASKCREATE(FARPROC fn, HTASK FAR *ph, DWORD inst)
{
    struct {
        WORD env;
        LPSTR cmd;
        LPVOID show;
        DWORD reserved;
    } pb;
    static char cmd[2] = { 0, 0x0D };
    static WORD show[2] = { 2, SW_SHOWNORMAL };
    HINSTANCE hi;
    struct mmtaskdata FAR *d;
    pb.env = 0;
    pb.cmd = cmd;
    pb.show = show;
    pb.reserved = 0;
    hi = LoadModule("mmtask.tsk", &pb);
    if ((UINT)hi < 32)
        return (UINT)hi == 0 || (UINT)hi == 8 ? TASKERR_OUTOFMEMORY : TASKERR_NOTASKSUPPORT;
    d = (struct mmtaskdata FAR *)GlobalLock((HGLOBAL)hi);
    if (!d)
        return TASKERR_NOTASKSUPPORT;
    d->fn = fn;
    d->inst = inst;
    GlobalUnlock((HGLOBAL)hi);
    /* it runs when the caller next yields (as 3.1's: the function's first
       call comes after mmTaskCreate has returned) */
    if (ph)
        *ph = (HTASK)taskof(hi);
    return 0;
}

UINT MMAPI MMTASKBLOCK(HTASK h)
{
    MSG m;
    (void)h;
    for (;;) {
        GetMessage(&m, 0, 0, 0);
        if (!m.hwnd)
            return m.message;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

void MMAPI MMTASKYIELD(void)
{
    MSG m;
    PeekMessage(&m, 0, 0, 0, PM_NOREMOVE);
}
