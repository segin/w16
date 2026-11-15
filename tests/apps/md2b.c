/*
 * md2b.c - MD2: MOUSE.DRV's PowerEventProc (ordinal 7), observed black-box
 * (tests/apps/md2logs/b/notes.md). Run as the shell, with MS3 (tests/dos/ms3b.asm)
 * loaded before WIN so that every PS/2 BIOS call (INT 15h C2xxh), every
 * APM BIOS call (INT 15h AH=53h), and, while one call is in progress, every
 * other INT 15h and INT 2Fh call that reaches real mode is recorded.
 *
 * [w16] md2phase in SYSTEM.INI picks what is done:
 *   1  the sweep: PowerEventProc called with one word of its three (6
 *      bytes) set to a code and the others 0, for many codes, in each of
 *      the three positions; then marker triples; then the same codes with
 *      the driver disabled (Disable) and before it is enabled again.
 *      Logged per call: the words, DX:AX, the bytes taken off, the flags,
 *      and the MS3 records the call caused.
 *   2  the event test: MD2's event procedure (md1s.asm's) enabled; while
 *      tests/scripts/input/md2b.txt moves the mouse, PowerEventProc is
 *      called with the codes in [w16] md2codes (word 1, the first pushed)
 *      after every md2every events, and a marker goes into the event list.
 *      Then the events, each with the PS/2 packets the BIOS gave the C207h
 *      handler, are listed.
 *   3  the power test (tests/md2bprobe.sh 3: POWER.DRV installed, MS4, an APM
 *      BIOS of ours, loaded before MS3): MD2 has a window and logs every
 *      WM_POWER it gets; after md2secs seconds the MS3 log is listed: the
 *      APM calls POWER.DRV made, the PS/2 BIOS calls the mouse driver
 *      made, and, with w16's driver built with -DPWRTRACE (MOUSEPT.DRV),
 *      INT 15h AX=C2E0h-C2E4h (PowerEventProc: its three words, first
 *      pushed first, its caller's CS and IP) and C2E8h-C2EDh
 *      (GetSetMouseData: four words, CS, IP); each caller's CS is named
 *      (ToolHelp's GlobalEntryHandle: the owning module).
 * The args array is pushed first element first: args[13] is the first
 * word pushed (a Pascal function's first parameter), args[15] the last.
 */
#include <windows.h>
#include <dos.h>
#include <toolhelp.h>

#define TK_REF "md1ref.h"
#include "tklog.h"

extern BYTE __far __cdecl md_code[];
extern WORD __far __cdecl md_len;
extern DWORD __cdecl __far md_call(FARPROC f, WORD FAR *args, WORD FAR *out);
extern int FAR PASCAL U_INITAPP(WORD hinst);

#define D_SEL   0
#define D_N     2
#define D_MAX   4
#define D_NEXT  6
#define D_NEST  12
#define H_OFF   16
#define D_REC   256
#define MAXREC  1000

static WORD args[16];
static WORD res[2];
static WORD FAR *ms;            /* MS3's header, or 0 */
static WORD msnext;
static FARPROC p[9];
static char line[120];

static void clear(void)
{
    int i;
    for (i = 0; i < 16; i++)
        args[i] = 0;
}

/* the MS3 records since the last call, all but INT 74h in/out */
static void msdump(void)
{
    WORD n, i, k = 0;
    char line[80];
    if (!ms)
        return;
    n = ms[0];
    for (i = msnext; i < n; i++) {
        WORD FAR *e = (WORD FAR *)MAKELP(SELECTOROF(ms), ms[10] + i * 16);
        if (e[0] == 3 || e[0] == 4 || e[0] == 6) {
            k++;
            continue;
        }
        wsprintf(line, "  R%u %04X %04X %04X %04X #%u\r\n", e[0], e[1], e[2], e[3], e[4], e[7]);
        out(line);
    }
    if (k) {
        wsprintf(line, "  (%u INT 74h / handler-return records)\r\n", k);
        out(line);
    }
    msnext = n;
}

static DWORD pwr(WORD a, WORD b, WORD c)
{
    DWORD r;
    msdump();
    clear();
    args[13] = a;
    args[14] = b;
    args[15] = c;
    if (ms)
        ms[13] = 1;
    r = md_call(p[7], args, res);
    if (ms)
        ms[13] = 0;
    wsprintf(line, "P %04X %04X %04X -> DX:AX=%04X:%04X popped=%u fl=%04X\r\n",
             a, b, c, HIWORD(r), LOWORD(r), res[0], res[1] & 0x0CD5);
    out(line);
    msdump();
    return r;
}

static void wait_ms(WORD msecs)
{
    MSG m;
    DWORD end = GetTickCount() + msecs;
    SetTimer(0, 1, 55, NULL);
    while ((long)(GetTickCount() - end) < 0)
        if (GetMessage(&m, 0, 0, 0))
            DispatchMessage(&m);
    KillTimer(0, 1);
}

static void named(const char *s)
{
    msdump();
    out(s);
    out("\r\n");
}

static WORD codes[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x0A, 0x0B, 0x0C, 0x0F, 0x10,
                        0x48, 0x80, 0x100, 0x5300, 0x8000, 0xFFFF };
#define NCODES (sizeof codes / sizeof codes[0])

static void sweep(int all)
{
    WORD i;
    for (i = 0; i < NCODES; i++)
        if (all || codes[i] <= 0x0C)
            pwr(codes[i], 0, 0);
    for (i = 0; i < NCODES; i++)
        if (all || codes[i] <= 0x0C)
            pwr(0, codes[i], 0);
    for (i = 0; i < NCODES; i++)
        if (all || codes[i] <= 0x0C)
            pwr(0, 0, codes[i]);
}

static void phase1(void)
{
    WORD i;
    DWORD r;
    named("== sweep, the driver enabled (USER's event procedure)");
    sweep(1);
    named("== markers");
    pwr(0x1111, 0x2222, 0x3333);
    pwr(0x0002, 0x1234, 0x5678);
    pwr(0x0003, 0x1234, 0x5678);
    pwr(0x1234, 0x5678, 0x0002);
    pwr(0x1234, 0x5678, 0x0003);
    named("== the same call twice in a row: (2,0,0) (2,0,0) (3,0,0) (3,0,0) (1,0,0) (1,0,0)");
    pwr(2, 0, 0);
    pwr(2, 0, 0);
    pwr(3, 0, 0);
    pwr(3, 0, 0);
    pwr(1, 0, 0);
    pwr(1, 0, 0);
    named("== the words below the parameters set to 5A5Ah");
    for (i = 0; i < 13; i++)
        args[i] = 0x5A5A;
    msdump();
    args[13] = 3;
    args[14] = 0;
    args[15] = 0;
    r = md_call(p[7], args, res);
    wsprintf(line, "P(5A5A below) 0003 0000 0000 -> DX:AX=%04X:%04X popped=%u\r\n",
             HIWORD(r), LOWORD(r), res[0]);
    out(line);
    msdump();
    named("== Disable");
    clear();
    md_call(p[3], args, res);
    named("== sweep, the driver disabled");
    sweep(0);
    named("== Enable (USER's event procedure)");
    clear();
    {
        FARPROC ume = GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(299));
        args[14] = SELECTOROF(ume);
        args[15] = OFFSETOF(ume);
        md_call(p[2], args, res);
    }
    named("== after Enable: (1,0,0) (2,0,0) (3,0,0) (4,0,0)");
    pwr(1, 0, 0);
    pwr(2, 0, 0);
    pwr(3, 0, 0);
    pwr(4, 0, 0);
    named("== GetSetMouseData (0,0,0,0) for comparison");
    clear();
    r = md_call(p[5], args, res);
    wsprintf(line, "G -> DX:AX=%04X:%04X popped=%u\r\n", HIWORD(r), LOWORD(r), res[0]);
    out(line);
    msdump();
}

/* the event test */
static void phase2(void)
{
    WORD sel, csel, i, n, every, ncodes, ci = 0, last = 0, secs;
    WORD FAR *w;
    WORD pc[16];
    FARPROC ume;
    char buf[80], *s;
    WORD tick;
    every = GetPrivateProfileInt("w16", "md2every", 3, "SYSTEM.INI");
    secs = GetPrivateProfileInt("w16", "md2secs", 40, "SYSTEM.INI");
    GetPrivateProfileString("w16", "md2codes", "2", buf, sizeof buf, "SYSTEM.INI");
    for (ncodes = 0, s = buf; *s && ncodes < 16; ncodes++) {
        WORD v = 0;
        while (*s == ' ' || *s == ',')
            s++;
        while (*s >= '0' && *s <= '9')
            v = v * 10 + (*s++ - '0');
        pc[ncodes] = v;
        while (*s == ' ' || *s == ',')
            s++;
    }
    wsprintf(line, "phase 2: every %u events, %u codes, %u s\r\n", every, ncodes, secs);
    out(line);
    sel = (WORD)GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, D_REC + MAXREC * 16L);
    if (!sel)
        return;
    w = (WORD FAR *)MAKELP(sel, 0);
    for (i = 0; i < md_len; i++)
        ((BYTE FAR *)w)[H_OFF + i] = md_code[i];
    ume = GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(299));
    w[D_SEL / 2] = sel;
    w[D_MAX / 2] = MAXREC;
    w[D_NEXT / 2] = OFFSETOF(ume);
    w[D_NEXT / 2 + 1] = SELECTOROF(ume);
    csel = AllocDStoCSAlias(sel);
    named("== Disable");
    clear();
    md_call(p[3], args, res);
    named("== Enable (MD2's event procedure)");
    clear();
    args[14] = csel;
    args[15] = H_OFF;
    md_call(p[2], args, res);
    named("listening");
    for (tick = 0; tick < secs * 10; tick++) {
        wait_ms(100);
        n = w[D_N / 2];
        if (ci < ncodes && n >= last + every) {
            /* a marker into the event list: status FFFFh, X = the code */
            WORD m;
            _disable();
            m = w[D_N / 2];
            if (m < MAXREC) {
                WORD FAR *e = w + (D_REC + m * 16) / 2;
                e[0] = 0xFFFF;
                e[1] = pc[ci];
                e[2] = ms ? ms[0] : 0;  /* MS3's record count at the time */
                w[D_N / 2] = m + 1;
            }
            _enable();
            wsprintf(line, "== calling PowerEventProc(%u, 0, 0) after event %u\r\n", pc[ci], m);
            named(line);
            pwr(pc[ci], 0, 0);
            ci++;
            last = w[D_N / 2];
        }
    }
    clear();
    md_call(p[3], args, res);
    clear();
    args[14] = SELECTOROF(ume);
    args[15] = OFFSETOF(ume);
    md_call(p[2], args, res);
    out("USER's event procedure back\r\n");
    /* the events, and the packets of the BIOS between them */
    n = w[D_N / 2];
    wsprintf(line, "events %u\r\n", n);
    out(line);
    for (i = 0; i < n; i++) {
        WORD FAR *e = w + (D_REC + i * 16) / 2;
        if (e[0] == 0xFFFF)
            wsprintf(line, "M code=%u ms3count=%u\r\n", e[1], e[2]);
        else
            wsprintf(line, "E %04X %d %d %u IF=%u\r\n", e[0], (int)e[1], (int)e[2], e[3],
                     (e[6] >> 9) & 1);
        out(line);
    }
    if (ms) {
        n = ms[0];
        out("packets (MS3 kind 5: status, x, y) and C2xx calls, all\r\n");
        for (i = 0; i < n; i++) {
            WORD FAR *e = (WORD FAR *)MAKELP(SELECTOROF(ms), ms[10] + i * 16);
            if (e[0] == 5 || e[0] == 1 || e[0] == 2 || e[0] == 10 || e[0] == 11) {
                wsprintf(line, "R%u %04X %04X %04X %04X #%u i=%u\r\n", e[0], e[1], e[2], e[3],
                         e[4], e[7], i);
                out(line);
            }
        }
    }
}

static WORD powcount;
LRESULT FAR PASCAL __export __loadds wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    char t[100];
    if (m == 0x0048) {          /* WM_POWER */
        powcount++;
        wsprintf(t, "WM_POWER wParam=%u lParam=%08lX ms3count=%u\r\n", wp, lp, ms ? ms[0] : 0);
        out(t);
        return wp == 1 ? 1 : 0;     /* PWR_OK to a suspend request */
    }
    return DefWindowProc(h, m, wp, lp);
}

static void modname(WORD sel, char *buf)
{
    typedef BOOL (FAR PASCAL *GEH)(GLOBALENTRY FAR *, HGLOBAL);
    static GEH geh;
    GLOBALENTRY ge;
    buf[0] = 0;
    if (!geh) {
        HINSTANCE th = LoadLibrary("TOOLHELP.DLL");
        if ((UINT)th > 32)
            geh = (GEH)GetProcAddress(th, "GlobalEntryHandle");
    }
    if (!geh)
        return;
    ge.dwSize = sizeof ge;
    if (geh(&ge, (HGLOBAL)LOWORD(GlobalHandle(sel))))
        GetModuleFileName((HINSTANCE)ge.hOwner, buf, 80);
}

static void phase3(void)
{
    WNDCLASS wc;
    HWND hw;
    WORD i, n, tick, secs;
    char name[82];
    secs = GetPrivateProfileInt("w16", "md2secs", 55, "SYSTEM.INI");
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = (HINSTANCE)init_regs[5];
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "MD2";
    RegisterClass(&wc);
    hw = CreateWindow("MD2", "MD2 power", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 10, 200, 80,
                      NULL, NULL, (HINSTANCE)init_regs[5], NULL);
    obs("window", hw != 0);
    named("phase 3: listening");
    for (tick = 0; tick < secs * 10; tick++)
        wait_ms(100);
    wsprintf(line, "WM_POWER messages %u\r\n", powcount);
    out(line);
    if (!ms)
        return;
    n = ms[0];
    out("MS3 records (no INT 74h), with the callers named\r\n");
    for (i = 0; i < n; i++) {
        WORD FAR *e = (WORD FAR *)MAKELP(SELECTOROF(ms), ms[10] + i * 16);
        if (e[0] == 3 || e[0] == 4 || e[0] == 6)
            continue;
        wsprintf(line, "R%u %04X %04X %04X %04X #%u", e[0], e[1], e[2], e[3], e[4], e[7]);
        out(line);
        if (e[0] == 1 && (e[1] == 0xC2E3 || e[1] == 0xC2EC)) {
            modname(e[2], name);
            out(" caller module \"");
            out(name);
            out("\"");
        }
        out("\r\n");
    }
}

int __cdecl tmain(void)
{
    HMODULE hm = GetModuleHandle("MOUSE");
    WORD i, phase;
    U_INITAPP(init_regs[5]);
    out("MD2 start\r\n");
    obs("module", hm != 0);
    if (!hm)
        return 1;
    GetModuleFileName(hm, line, sizeof line);
    out("OBS ~file=\"");
    out(line);
    out("\"\r\n");
    for (i = 1; i <= 8; i++)
        p[i] = GetProcAddress(hm, MAKEINTRESOURCE(i));
    obs("entry 7", p[7] != NULL);
    obs("entry 7 by name", GetProcAddress(hm, "POWEREVENTPROC") == p[7]);
    {
        WORD ivtsel = (WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0000H");
        WORD FAR *ivt = (WORD FAR *)MAKELP(ivtsel, 0);
        WORD off = ivt[0x66 * 2], seg = ivt[0x66 * 2 + 1];
        WORD ls = AllocSelector(SELECTOROF((void FAR *)&i));
        SetSelectorBase(ls, (DWORD)seg << 4);
        SetSelectorLimit(ls, 0xFFFF);
        ms = (WORD FAR *)MAKELP(ls, off);
        if (!seg || ms[1] != 3000)
            ms = 0;
    }
    obs("MS3 loaded", ms != 0);
    named("== records before MD2's first call (boot: LibEntry, USER's Enable)");
    phase = GetPrivateProfileInt("w16", "md2phase", 1, "SYSTEM.INI");
    if (phase == 1)
        phase1();
    else if (phase == 2)
        phase2();
    else
        phase3();
    msdump();
    out("MD2 done\r\n");
    return 0;
}
