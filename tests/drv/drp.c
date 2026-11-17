/*
 * drp.c - DRP.EXE, the installable-driver probe (ARCHITECTURE.md 10.7):
 * USER's OpenDriver, CloseDriver, SendDriverMessage, DefDriverProc,
 * GetDriverModuleHandle, GetDriverInfo and GetNextDriver, against the
 * driver DRT.DLL (tests/drv/drt.c), which logs every DriverProc call into
 * the same log, C:\DRP.LOG. Run as the shell (tests/drv/drun.sh) with
 * Microsoft's USER and with w16's; the logs must be the same.
 *
 * Sections ("== NAME"): def (DefDriverProc's answers), enum (the drivers
 * open before the probe), open (first and second instances, by [drivers]
 * name, by a section, by file name, a second alias of the same file,
 * names not there), info (GetDriverModuleHandle, GetDriverInfo,
 * GetNextDriver), send (SendDriverMessage, DefDriverProc with an open
 * handle), galloc (GlobalAlloc's handles for a program and a DLL: KERNEL,
 * found while chasing QuickTime's hang), fail (DRV_OPEN, DRV_LOAD,
 * DRV_CLOSE answered 0), child (a second task opens an instance and ends
 * without closing it), close (the rest closed), exit (one instance left
 * open when the shell ends: what the driver gets as Windows ends).
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 1 ch. 25
 * (installable drivers), Vol. 2 (the functions), Vol. 3
 * (DRIVERINFOSTRUCT).
 */
#include <windows.h>


extern WORD __cdecl entry_regs[11];  /* start.asm: ax bx cx dx si di bp sp ds es ss */
static const char logname[] = "C:\\DRP.LOG";
static char line[300];

static void put(const char FAR *s, int n)
{
    HFILE f = _lopen(logname, OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat(logname, 0);
    else
        _llseek(f, 0, 2);
    _lwrite(f, s, n);
    _lclose(f);
}

static void lg(const char *fmt, ...)
{
    int n = wvsprintf(line, fmt, (LPSTR)(&fmt + 1));
    line[n++] = '\r';
    line[n++] = '\n';
    put(line, n);
}

/* what DRT.DLL answers 0, and the probe's task, for its log (drt.c) */
static void DRT_SET(WORD fail, HTASK task)
{
    char b[8];
    wsprintf(b, "%u", fail);
    WritePrivateProfileString("drt", "fail", b, "SYSTEM.INI");
    wsprintf(b, "%u", (WORD)task);
    WritePrivateProfileString("drt", "task", b, "SYSTEM.INI");
}

static HMODULE drtmod(void)
{
    return GetModuleHandle("DRT");
}

static HDRVR opendrv(LPCSTR name, LPCSTR sec, LPARAM l)
{
    HDRVR h;
    lg("OpenDriver(%s, %s, %08lX)", name ? name : (LPCSTR)"NULL",
       sec ? sec : (LPCSTR)"NULL", l);
    h = OpenDriver(name, sec, l);
    lg(" = %04X; DRT usage %d", (WORD)h, drtmod() ? GetModuleUsage(drtmod()) : 0);
    return h;
}

static void closedrv(HDRVR h, LPARAM l1, LPARAM l2)
{
    LRESULT r;
    lg("CloseDriver(%04X, %08lX, %08lX)", (WORD)h, l1, l2);
    r = CloseDriver(h, l1, l2);
    lg(" = %08lX; DRT usage %d", r, drtmod() ? GetModuleUsage(drtmod()) : 0);
}

/* what GetDriverInfo tells of h (with the given length) */
static void info(HDRVR h, UINT len)
{
    static DRIVERINFOSTRUCT d;
    BOOL r;
    int i;
    for (i = 0; i < (int)sizeof d; i++)
        ((BYTE *)&d)[i] = 0xEE;
    d.length = len;
    r = GetDriverInfo(h, &d);
    d.szAliasName[sizeof d.szAliasName - 1] = 0;
    lg("GetDriverInfo(%04X, len %u) = %d: length %04X hDriver %04X hModule %s \"%s\"",
       (WORD)h, len, r, d.length, (WORD)d.hDriver,
       d.hModule == (HINSTANCE)0xEEEE ? (LPSTR)"EEEE" :
       d.hModule == drtmod() ? (LPSTR)"DRT" :
       d.hModule == GetModuleHandle("MMSYSTEM") ? (LPSTR)"MMSYSTEM" :
       d.hModule == GetModuleHandle("TIMER") ? (LPSTR)"TIMER" :
       d.hModule ? (LPSTR)"other" : (LPSTR)"0",
       (BYTE)d.szAliasName[0] == 0xEE ? (LPSTR)"(untouched)" : (LPSTR)d.szAliasName);
}

static void modh(HDRVR h)
{
    HINSTANCE m = GetDriverModuleHandle(h);
    lg("GetDriverModuleHandle(%04X) = %s", (WORD)h,
       m == drtmod() && m ? (LPSTR)"DRT" :
       m == GetModuleHandle("MMSYSTEM") && m ? (LPSTR)"MMSYSTEM" :
       m == GetModuleHandle("TIMER") && m ? (LPSTR)"TIMER" :
       m ? (LPSTR)"other" : (LPSTR)"0");
}

/* every driver GetNextDriver gives from 0 with the flags */
static void enumerate(DWORD flags)
{
    HDRVR h = 0;
    int k;
    char *p = line;
    p += wsprintf(p, "GetNextDriver flags %ld:", flags);
    for (k = 0; k < 20; k++) {
        h = GetNextDriver(h, flags);
        p += wsprintf(p, " %04X", (WORD)h);
        if (!h)
            break;
    }
    *p++ = '\r';
    *p++ = '\n';
    put(line, p - line);
}

static void pump(void)
{
    MSG m;
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

int __cdecl __far tmain(void)
{
    static HDRVR a, b, c, d, e, f, g;
    ATOM child = GlobalFindAtom("DRPCHILD");
    HTASK me = GetCurrentTask();
    WORD i;
    LRESULT r;

    /* USER's InitApp (USER.5), as a program's start-up code calls it: the
       boot drivers ([boot] drivers=) are opened at the shell's (US1) */
    {
        typedef BOOL (FAR PASCAL *INITAPP)(HINSTANCE);
        INITAPP ia = (INITAPP)GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(5));
        HINSTANCE hi = (HINSTANCE)entry_regs[5];   /* DI: the instance */
        if (!child)
            lg("InitApp");
        ia(hi);
    }
    if (child) {
        GlobalDeleteAtom(child);
        lg("child: start");
        g = opendrv("drt", NULL, 0xC1);
        lg("child: end, %04X left open", (WORD)g);
        wsprintf(line, "%u", (WORD)g);
        WritePrivateProfileString("drt", "child", line, "SYSTEM.INI");
        return 0;
    }
    WriteProfileString(NULL, NULL, NULL);
    WritePrivateProfileString("drivers", "drt", "drt.dll first second", "SYSTEM.INI");
    WritePrivateProfileString("drivers", "drt2", "DRT.DLL", "SYSTEM.INI");
    WritePrivateProfileString("drivers", "drtx", "nothere.dll x", "SYSTEM.INI");
    WritePrivateProfileString("drtsec", "drt", "drt.dll insec", "SYSTEM.INI");
    DRT_SET(0, 0);
    lg("DRP start");
    lg("== def");
    for (i = 0; i <= 16; i++) {
        r = DefDriverProc(0, 0, i, 0, 0);
        lg("DefDriverProc(0, 0, %04X) = %08lX", i, r);
    }
    lg("DefDriverProc(1, 5, DRV_INSTALL, 1, 2) = %08lX", DefDriverProc(1, (HDRVR)5, DRV_INSTALL, 1, 2));
    lg("DefDriverProc(0, 0, DRV_RESERVED) = %08lX", DefDriverProc(0, 0, DRV_RESERVED, 0, 0));
    lg("DefDriverProc(0, 0, DRV_USER) = %08lX", DefDriverProc(0, 0, DRV_USER, 0, 0));

    lg("== enum");
    enumerate(0);
    enumerate(GND_FIRSTINSTANCEONLY);
    enumerate(GND_REVERSE);
    enumerate(GND_REVERSE | GND_FIRSTINSTANCEONLY);
    for (i = 1; i <= 4; i++) {
        info((HDRVR)i, sizeof(DRIVERINFOSTRUCT));
        modh((HDRVR)i);
    }

    lg("== open");
    a = opendrv("drt", NULL, 0x12345678L);
    DRT_SET(0, me);
    b = opendrv("drt", NULL, 7);
    c = opendrv("drt", "drtsec", 3);
    d = opendrv("DRT.DLL", NULL, 4);
    e = opendrv("drt2", NULL, 5);
    opendrv("drtx", NULL, 6);
    opendrv("nothere", NULL, 6);
    opendrv("drt", "nosuchsec", 6);
    opendrv("", NULL, 6);

    lg("== info");
    modh(a);
    modh(b);
    modh(0);
    modh((HDRVR)0x99);
    info(a, sizeof(DRIVERINFOSTRUCT));
    info(c, sizeof(DRIVERINFOSTRUCT));
    info(d, sizeof(DRIVERINFOSTRUCT));
    info(e, sizeof(DRIVERINFOSTRUCT));
    info(a, 0);
    info(a, sizeof(DRIVERINFOSTRUCT) + 2);
    info(0, sizeof(DRIVERINFOSTRUCT));
    info((HDRVR)0x99, sizeof(DRIVERINFOSTRUCT));
    enumerate(0);
    enumerate(GND_FIRSTINSTANCEONLY);
    enumerate(GND_REVERSE);
    enumerate(GND_REVERSE | GND_FIRSTINSTANCEONLY);
    lg("GetNextDriver(%04X, 0) = %04X", (WORD)b, (WORD)GetNextDriver(b, 0));
    lg("GetNextDriver(%04X, REVERSE) = %04X", (WORD)b, (WORD)GetNextDriver(b, GND_REVERSE));
    lg("GetNextDriver(%04X, FIRST) = %04X", (WORD)b, (WORD)GetNextDriver(b, GND_FIRSTINSTANCEONLY));
    lg("GetNextDriver(0099, 0) = %04X", (WORD)GetNextDriver((HDRVR)0x99, 0));

    lg("== send");
    lg("SendDriverMessage(b, DRV_USER+1, 1, 2) = %08lX", SendDriverMessage(b, DRV_USER + 1, 1, 2));
    lg("== galloc");
    {
        /* GlobalAlloc's handle (its low two bits) for a program and, through
           DRT's DRV_USER+2, for a DLL: under KRNL386 a program's GMEM_FIXED
           block is moveable (an even handle), a DLL's fixed (the selector,
           odd); under KRNL286 both are fixed (drun.sh runs both kernels) */
        static const WORD fl[] = { 0, 0x2000, 0x0002, 0x2002, 0x0040, 0x2040, 0x1000, 0x0100 };
        for (i = 0; i < 8; i++) {
            HGLOBAL g = GlobalAlloc(fl[i], 0x600);
            lg("program GlobalAlloc(%04X, 600h): handle & 3 = %u, GlobalFlags %04X",
               fl[i], (WORD)g & 3, GlobalFlags(g));
            GlobalFree(g);
            lg("DLL GlobalAlloc(%04X, 600h): %08lX", fl[i], SendDriverMessage(b, DRV_USER + 2, fl[i], 0));
        }
    }
    {
        /* FS and GS holding a block's selector across GlobalFree of the
           block (QuickTime frees blocks it still has in FS) */
        static WORD sel, rfs, rgs, res;
        static HGLOBAL g;
        static FARPROC gf;
        static WORD other;
        for (i = 0; i < 3; i++) {
            g = GlobalAlloc(i == 1 ? GMEM_MOVEABLE : GMEM_FIXED, 0x100);
            sel = HIWORD(GlobalLock(g));
            GlobalUnlock(g);
            gf = GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(17));
            _asm {
                mov ax, sel
                db 8Eh, 0E0h            /* mov fs, ax */
                db 8Eh, 0E8h            /* mov gs, ax */
                cmp i, 2                /* the third time: GS = DS */
                jne gsset
                mov ax, ds
                mov other, ax
                db 8Eh, 0E8h
                mov ax, sel
            gsset:
                push es
                mov es, ax
                push g
                call dword ptr gf
                mov res, es
                pop es
                db 8Ch, 0E0h            /* mov ax, fs */
                mov rfs, ax
                db 8Ch, 0E8h            /* mov ax, gs */
                mov rgs, ax
                xor ax, ax
                db 8Eh, 0E0h
                db 8Eh, 0E8h
            }
            lg("GlobalFree(%s block in FS, %s, ES): FS %s, GS %s, ES %s",
               i == 1 ? (LPSTR)"moveable" : (LPSTR)"fixed", i == 2 ? (LPSTR)"DS in GS" : (LPSTR)"GS",
               rfs == sel ? (LPSTR)"kept" : rfs ? (LPSTR)"other" : (LPSTR)"0",
               rgs == sel || (i == 2 && rgs == other) ? (LPSTR)"kept" : rgs ? (LPSTR)"other" : (LPSTR)"0",
               res == sel ? (LPSTR)"kept" : res ? (LPSTR)"other" : (LPSTR)"0");
        }
    }
    lg("SendDriverMessage(a, DRV_QUERYCONFIGURE, 0, 0) = %08lX", SendDriverMessage(a, DRV_QUERYCONFIGURE, 0, 0));
    lg("SendDriverMessage(a, DRV_INSTALL, 0, 0) = %08lX", SendDriverMessage(a, DRV_INSTALL, 0, 0));
    lg("SendDriverMessage(a, DRV_LOAD, 0, 0) = %08lX", SendDriverMessage(a, DRV_LOAD, 0, 0));
    for (i = 0; i <= 16; i++) {
        r = DefDriverProc(0, b, i, 0, 0);
        lg("DefDriverProc(0, b, %04X) = %08lX", i, r);
    }
    lg("DefDriverProc(0, b, DRV_INSTALL, 1, 2) = %08lX", DefDriverProc(0, b, DRV_INSTALL, 1, 2));
    lg("DefDriverProc(0, b, DRV_REMOVE, 1, 2) = %08lX", DefDriverProc(0, b, DRV_REMOVE, 1, 2));
    lg("SendDriverMessage(0, DRV_USER, 0, 0) = %08lX", SendDriverMessage(0, DRV_USER, 0, 0));
    lg("SendDriverMessage(0099, DRV_USER, 0, 0) = %08lX", SendDriverMessage((HDRVR)0x99, DRV_USER, 0, 0));

    lg("== fail");
    DRT_SET(DRV_CLOSE, me);
    closedrv(e, 0xE1, 0xE2);
    modh(e);
    DRT_SET(DRV_OPEN, me);
    f = opendrv("drt", NULL, 8);
    modh(f);
    DRT_SET(0, me);
    closedrv(e, 0xE3, 0xE4);
    modh(e);
    closedrv(e, 0, 0);
    closedrv((HDRVR)0x99, 0, 0);
    closedrv(0, 0, 0);

    lg("== child");
    GlobalAddAtom("DRPCHILD");
    lg("WinExec >= 32: %d", WinExec("C:\\DRP.EXE", SW_SHOWNORMAL) >= 32);
    for (i = 0; i < 2000 && GetModuleUsage(GetModuleHandle("DRP")) > 1; i++) {
        pump();
        Yield();
    }
    lg("child gone: %d", GetModuleUsage(GetModuleHandle("DRP")) <= 1);
    enumerate(0);
    closedrv((HDRVR)GetPrivateProfileInt("drt", "child", 0, "SYSTEM.INI"), 0xC2, 0xC3);

    lg("== close");
    closedrv(b, 0xB1, 0xB2);
    closedrv(c, 0, 0);
    closedrv(d, 0, 0);
    enumerate(0);
    lg("== last");
    closedrv(a, 0xA1, 0xA2);
    enumerate(0);
    lg("== reopen");
    DRT_SET(DRV_LOAD, me);
    f = opendrv("drt", NULL, 9);
    enumerate(0);
    DRT_SET(DRV_OPEN, me);
    f = opendrv("drt", NULL, 9);
    enumerate(0);
    DRT_SET(0, me);
    f = opendrv("drt", NULL, 10);
    enumerate(0);
    lg("== exit");
    lg("DRP end (%04X left open)", (WORD)f);
    return 0;
}
