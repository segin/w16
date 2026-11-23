/*
 * vx16.c - VX16.EXE: where the time goes in 386 Enhanced mode (the
 * QuickTime speed work: ARCHITECTURE.md 12.14). Run as the shell or by
 * WIN.INI run= under Windows 3.1's WIN386 and under w16's
 * (tests/vx16run.sh), and in Standard mode for reference. Each phase
 * counts one kind of work for `secs` seconds of the real-time clock (CMOS
 * register 0: the host's clock in DOSBox, the same for every VM; a VM's
 * timer ticks are virtual):
 *   loop   a busy loop (the processor's speed for ring-3 code)
 *   tick   GetTickCount's advance in those seconds (ms: virtual timer
 *          ticks lost or delayed show as less than 1000 a second)
 *   tgt    MMSYSTEM's timeGetTime's advance (ms)
 *   tev    callbacks of a 10 ms periodic timeSetEvent (100 a second when
 *          none is lost)
 *   cli    CLI/STI pairs (each traps to the VMM in protected mode)
 *   pic    reads of the 8259's mask (port 21h: trapped by VPICD)
 *   int1a  INT 1Ah AH=00h from protected mode (reflected to V86 mode)
 *   dos    INT 21h AH=19h from protected mode (KERNEL's handler, then DOS)
 *   read   8 KB _lread calls from a 64 KB file (DOS reads through the
 *          DPMI host's translation)
 *   blt    BitBlt of a 128 x 128 memory bitmap to the screen
 *   dib    SetDIBitsToDevice of a 160 x 120 8-bit DIB to the screen
 *   peek   PeekMessage(PM_REMOVE) calls with no message waiting
 *   msgs   message round trips (PostAppMessage, GetMessage)
 * WIN.INI [vx16]: secs=4, wait=5 (seconds of messages before the first
 * phase), only=LIST (the phases to run, default all).
 * Output: TK.LOG as it goes; C:\VX16.LOG at the
 * end: lines "VX16 NAME=COUNT".
 * Black box (ARCHITECTURE.md 3): only the published API, the clock and
 * the documented ports.
 */
#include <windows.h>

extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

BYTE rtc_read(BYTE reg);
#pragma aux rtc_read = "out 70h,al" "in al,71h" parm [al] value [al];
void cli_sti(void);
#pragma aux cli_sti = "cli" "sti";
BYTE pic_read(void);
#pragma aux pic_read = "in al,21h" value [al];
void int1a(void);
#pragma aux int1a = "xor ah,ah" "int 1Ah" modify [ax cx dx];
void dos19(void);
#pragma aux dos19 = "mov ah,19h" "int 21h" modify [ax];
WORD flags(void);
#pragma aux flags = "pushf" "pop ax" value [ax];
void sti(void);
#pragma aux sti = "sti";

typedef DWORD (FAR PASCAL *TGT)(void);
typedef UINT (FAR PASCAL *TSE)(UINT, UINT, void (CALLBACK *)(UINT, UINT, DWORD, DWORD, DWORD), DWORD, UINT);
typedef UINT (FAR PASCAL *TKE)(UINT);
typedef UINT (FAR PASCAL *TBP)(UINT);

static char res[2048];
static int nres;
static int secs;
static char only[128];

static void out(const char *s)
{
    int n = lstrlen(s);
    log_write(s, n);
    if (nres + n < sizeof(res)) {
        lstrcpy(res + nres, s);
        nres += n;
    }
}

static BYTE rtc(void)
{
    while (rtc_read(0x0A) & 0x80)
        ;
    return rtc_read(0);
}

static void align(void)
{
    BYTE s = rtc();
    while (rtc() == s)
        ;
}

static void pump(int n)
{
    MSG m;
    BYTE s = rtc(), t;
    UINT id = SetTimer(0, 0, 100, NULL);
    while (n > 0 && GetMessage(&m, 0, 0, 0)) {
        DispatchMessage(&m);
        t = rtc();
        if (t != s) { s = t; n--; }
    }
    KillTimer(0, id);
}

static int want(const char *name)
{
    const char *p = only;
    int n = lstrlen(name);
    if (!*p) return 1;
    while (*p) {
        int i = 0;
        while (p[i] && p[i] != ',') i++;
        if (i == n) {
            int j = 0;
            while (j < n && p[j] == name[j]) j++;
            if (j == n) return 1;
        }
        p += i;
        if (*p) p++;
    }
    return 0;
}

static void report(const char *name, DWORD n)
{
    char line[64];
    wsprintf(line, "VX16 %s=%lu\r\n", (LPSTR)name, n);
    out(line);
}

/* the phases: a counter run for `secs` seconds; each step does some work
 * and returns how much */
static int k_left;
static BYTE k_sec;
static void start(void) { align(); k_sec = rtc(); k_left = secs; }
static int done(void)
{
    BYTE t = rtc();
    if (t != k_sec) { k_sec = t; if (--k_left == 0) return 1; }
    return 0;
}

static volatile DWORD tev_n;
static void CALLBACK tev_cb(UINT id, UINT msg, DWORD user, DWORD d1, DWORD d2)
{
    (void)id; (void)msg; (void)d1; (void)d2;
    ++*(volatile DWORD FAR *)user;
}

/* which calls come back with interrupts disabled (the flags' IF; each is
 * enabled again after): "VX16 ifoff=NAMES" ("-": none) */
static char ifoff[256];
static void ifnote(const char *name)
{
    if (flags() & 0x200) return;
    if (lstrlen(ifoff) + lstrlen(name) + 2 < sizeof(ifoff)) {
        lstrcat(ifoff, " ");
        lstrcat(ifoff, name);
    }
    sti();
}

static void ifcheck(void)
{
    MSG m;
    HDC dc;
    HFILE f;
    char t[32];
    ifnote("start");
    GetTickCount(); ifnote("GetTickCount");
    GetProfileInt("vx16", "x", 0); ifnote("GetProfileInt");
    wsprintf(t, "%d", 1); ifnote("wsprintf");
    log_write("", 0); ifnote("int21");
    dos19(); ifnote("dos19");
    int1a(); ifnote("int1a");
    PeekMessage(&m, 0, 0, 0, PM_NOREMOVE); ifnote("PeekMessage");
    PeekMessage(&m, 0, 0, 0, PM_REMOVE | PM_NOYIELD); ifnote("PeekNoYield");
    PostAppMessage(GetCurrentTask(), WM_USER, 0, 0); ifnote("PostAppMessage");
    GetMessage(&m, 0, 0, 0); ifnote("GetMessage");
    DispatchMessage(&m); ifnote("DispatchMessage");
    Yield(); ifnote("Yield");
    dc = GetDC(0); ifnote("GetDC");
    PatBlt(dc, 0, 0, 1, 1, DSTINVERT); ifnote("PatBlt");
    PatBlt(dc, 0, 0, 1, 1, DSTINVERT);
    ReleaseDC(0, dc); ifnote("ReleaseDC");
    GlobalFree(GlobalAlloc(GMEM_MOVEABLE, 100)); ifnote("GlobalAlloc");
    f = _lopen("C:\\NOFILE.X", OF_READ); ifnote("_lopen");
    if (f != HFILE_ERROR) _lclose(f);
    if (!ifoff[0]) lstrcpy(ifoff, " -");
}

static BYTE bmi_buf[sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD)];

int __cdecl tmain(void)
{
    HFILE f;
    DWORD n;
    HINSTANCE hmm;
    TGT tgt = NULL;
    TSE tse = NULL;
    TKE tke = NULL;
    TBP tbp = NULL, tep = NULL;
    static BYTE buf[8192];
    char line[96];

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    secs = GetProfileInt("vx16", "secs", 4);
    GetProfileString("vx16", "only", "", only, sizeof(only));
    hmm = LoadLibrary("MMSYSTEM.DLL");
    if ((UINT)hmm >= 32) {
        tgt = (TGT)GetProcAddress(hmm, "timeGetTime");
        tse = (TSE)GetProcAddress(hmm, "timeSetEvent");
        tke = (TKE)GetProcAddress(hmm, "timeKillEvent");
        tbp = (TBP)GetProcAddress(hmm, "timeBeginPeriod");
        tep = (TBP)GetProcAddress(hmm, "timeEndPeriod");
    }
    wsprintf(line, "VX16 start winflags=%lx secs=%d\r\n", GetWinFlags(), secs);
    out(line);
    ifcheck();
    pump(GetProfileInt("vx16", "wait", 5));
    ifnote("pump");
    wsprintf(line, "VX16 ifoff=%s\r\n", (LPSTR)(ifoff + 1));
    out(line);

    if (want("loop")) {
        n = 0;
        start();
        for (;;) { n++; if (!(n & 1023) && done()) break; }
        report("loop", n);
    }
    if (want("tick")) {
        DWORD a;
        start();
        a = GetTickCount();
        while (!done()) ;
        report("tick", GetTickCount() - a);
    }
    if (want("tgt") && tgt) {
        DWORD a;
        start();
        a = tgt();
        while (!done()) ;
        report("tgt", tgt() - a);
    }
    if (want("tev") && tse && tke) {
        UINT id;
        tev_n = 0;
        if (tbp) tbp(10);
        start();
        id = tse(10, 10, tev_cb, (DWORD)(void FAR *)&tev_n, 1 /* TIME_PERIODIC */);
        while (!done()) ;
        if (id) tke(id);
        if (tep) tep(10);
        report("tev", id ? tev_n : 0);
    }
    if (want("cli")) {
        n = 0;
        start();
        for (;;) { cli_sti(); n++; if (!(n & 255) && done()) break; }
        report("cli", n);
    }
    if (want("pic")) {
        n = 0;
        start();
        for (;;) { pic_read(); n++; if (!(n & 63) && done()) break; }
        report("pic", n);
    }
    if (want("int1a")) {
        n = 0;
        start();
        for (;;) { int1a(); n++; if (!(n & 63) && done()) break; }
        report("int1a", n);
    }
    if (want("dos")) {
        n = 0;
        start();
        for (;;) { dos19(); n++; if (!(n & 63) && done()) break; }
        report("dos", n);
    }
    if (want("read")) {
        int i;
        f = _lcreat("C:\\VX16.DAT", 0);
        if (f != HFILE_ERROR) {
            for (i = 0; i < 8; i++) _lwrite(f, (LPCSTR)buf, sizeof(buf));
            _lclose(f);
        }
        f = _lopen("C:\\VX16.DAT", OF_READ);
        n = 0;
        if (f != HFILE_ERROR) {
            start();
            for (;;) {
                _llseek(f, (n & 7) * 8192L, 0);
                _lread(f, buf, sizeof(buf));
                n++;
                if (!(n & 3) && done()) break;
            }
            _lclose(f);
        }
        report("read", n);
    }
    if (want("blt")) {
        HDC sdc = GetDC(0), mdc = CreateCompatibleDC(sdc);
        HBITMAP bm = CreateCompatibleBitmap(sdc, 128, 128), ob = SelectObject(mdc, bm);
        int i;
        for (i = 0; i < 128; i += 8) PatBlt(mdc, 0, i, 128, 4, (i & 8) ? BLACKNESS : WHITENESS);
        n = 0;
        start();
        for (;;) {
            BitBlt(sdc, 200 + (int)(n & 7), 100, 128, 128, mdc, 0, 0, SRCCOPY);
            n++;
            if (!(n & 3) && done()) break;
        }
        SelectObject(mdc, ob);
        DeleteObject(bm);
        DeleteDC(mdc);
        ReleaseDC(0, sdc);
        report("blt", n);
    }
    if (want("dib")) {
        HDC sdc = GetDC(0);
        BITMAPINFO FAR *bi = (BITMAPINFO FAR *)bmi_buf;
        HGLOBAL hb = GlobalAlloc(GMEM_MOVEABLE, 160L * 120);
        BYTE FAR *bits = (BYTE FAR *)GlobalLock(hb);
        int i;
        bi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi->bmiHeader.biWidth = 160;
        bi->bmiHeader.biHeight = 120;
        bi->bmiHeader.biPlanes = 1;
        bi->bmiHeader.biBitCount = 8;
        bi->bmiHeader.biCompression = BI_RGB;
        for (i = 0; i < 256; i++) {
            bi->bmiColors[i].rgbRed = (BYTE)i;
            bi->bmiColors[i].rgbGreen = (BYTE)(i * 3);
            bi->bmiColors[i].rgbBlue = (BYTE)(255 - i);
            bi->bmiColors[i].rgbReserved = 0;
        }
        for (i = 0; i < 160 * 120; i++) bits[i] = (BYTE)(i ^ (i >> 7));
        n = 0;
        if (bits) {
            start();
            for (;;) {
                SetDIBitsToDevice(sdc, 200, 260, 160, 120, 0, 0, 0, 120, bits, bi, DIB_RGB_COLORS);
                n++;
                if (done()) break;
            }
        }
        GlobalUnlock(hb);
        GlobalFree(hb);
        ReleaseDC(0, sdc);
        report("dib", n);
    }
    if (want("peek")) {
        MSG m;
        n = 0;
        start();
        for (;;) {
            if (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) DispatchMessage(&m);
            n++;
            if (!(n & 15) && done()) break;
        }
        report("peek", n);
    }
    if (want("msgs")) {
        MSG m;
        HTASK me = GetCurrentTask();
        n = 0;
        start();
        for (;;) {
            PostAppMessage(me, WM_USER, 0, 0);
            if (!GetMessage(&m, 0, 0, 0)) break;
            if (m.message != WM_USER) DispatchMessage(&m);
            n++;
            if (!(n & 15) && done()) break;
        }
        report("msgs", n);
    }
    if ((UINT)hmm >= 32) FreeLibrary(hmm);
    out("VX16 done\r\n");
    f = _lcreat("C:\\VX16.LOG", 0);
    if (f != HFILE_ERROR) {
        _lwrite(f, res, nres);
        _lclose(f);
    }
    return 0;
}
