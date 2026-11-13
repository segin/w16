/*
 * md1.c - MD1: the mouse driver's interface, observed (drivers/mouse/
 * README.md). Run as the shell under tests/inputrun.sh (the script
 * tests/scripts/input/mouseprobe.txt moves and clicks while it listens),
 * once with Windows 3.1's MOUSE.DRV and once with w16's, and compare the
 * TK.LOGs (tests/mouseprobe.sh does all of it).
 *
 * Logged: the module's file and entry points; Inquire's MOUSEINFO (the
 * buffer filled with CCh first, 32 bytes dumped), its return value and
 * how many bytes it pops; MouseGetIntVect; CplApplet with NEWINQUIRE (8)
 * and INQUIRE (3); then the event procedure: Disable, Enable with MD1's
 * own procedure (md1s.asm), which records every call's registers and
 * passes it on to USER's Mouse_Event (USER.299), for [w16] md1secs
 * seconds (default 50) of input, then Enable with USER's again. Each call
 * is listed (E status x y buttons si di IF) and summed up: motion totals,
 * how often each status bit came, the button counts, SI/DI and IF seen,
 * the deepest nesting. Last, GetSetMouseData (5) and PowerEventProc (7)
 * are called with sixteen zero words on the stack, to see how many they
 * take off (nothing in Windows 3.1 imports them; what they do is
 * unknown).
 */
#include <windows.h>

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

static DWORD call(FARPROC f)
{
    return md_call(f, args, res);
}

static void clear(void)
{
    int i;
    for (i = 0; i < 16; i++)
        args[i] = 0;
}

static void wait(WORD secs)
{
    MSG m;
    WORD i;
    SetTimer(0, 1, 1000, NULL);
    for (i = 0; i < secs; i++)
        while (GetMessage(&m, 0, 0, 0) && m.message != WM_TIMER)
            DispatchMessage(&m);
    KillTimer(0, 1);
}

static void bdump(const char *name, BYTE FAR *b, WORD n)
{
    char line[16];
    WORD i;
    out("OBS ");
    out(name);
    out("=");
    for (i = 0; i < n; i++) {
        wsprintf(line, "%02X%s", b[i], (LPSTR)(i + 1 < n ? " " : ""));
        out(line);
    }
    out("\r\n");
}

/* a void function's AX is whatever it left: logged as "~" */
static void report(const char *name, DWORD r)
{
    char t[40];
    wsprintf(t, "%s AX", (LPSTR)name);
    obs(t, LOWORD(r));
    wsprintf(t, "%s popped", (LPSTR)(name + (name[0] == '~')));
    obs(t, res[0]);
}

int __cdecl tmain(void)
{
    HMODULE hm = GetModuleHandle("MOUSE");
    FARPROC p[9], ume;
    BYTE buf[256];
    char line[96];
    WORD i, n, sel, csel, secs;
    WORD FAR *w;
    DWORD r;
    long sx = 0, sy = 0;
    WORD cnt[16], dxs = 0, si = 0, di = 0, ifs = 0, other = 0, moves = 0;
    U_INITAPP(init_regs[5]);
    out("MD1 start\r\n");
    obs("module", hm != 0);
    if (!hm)
        return 1;
    GetModuleFileName(hm, line, sizeof line);
    out("OBS ~file=\"");
    out(line);
    out("\"\r\n");
    for (i = 1; i <= 8; i++) {
        p[i] = GetProcAddress(hm, MAKEINTRESOURCE(i));
        wsprintf(line, "entry %u", i);
        obs(line, p[i] != NULL);
    }
    obs("entry WEP by name", GetProcAddress(hm, "WEP") == p[8]);

    /* Inquire(LPMOUSEINFO) */
    for (i = 0; i < 32; i++)
        buf[i] = 0xCC;
    clear();
    args[14] = SELECTOROF((void FAR *)buf);
    args[15] = OFFSETOF((void FAR *)buf);
    r = call(p[1]);
    report("Inquire", r);
    bdump("MOUSEINFO", buf, 32);
    w = (WORD FAR *)buf;
    obs("msExist", buf[0]);
    obs("msRelative", buf[1]);
    obs("msNumButtons", w[1]);
    obs("msRate", w[2]);
    obs("msXThreshold", w[3]);
    obs("msYThreshold", w[4]);
    obs("msXRes", w[5]);
    obs("msYRes", w[6]);
    obs("msMouseCommPort", w[7]);

    /* MouseGetIntVect() */
    clear();
    r = call(p[4]);
    report("MouseGetIntVect", r);

    /* CplApplet(hwnd, NEWINQUIRE, 0, &NEWCPLINFO), then INQUIRE */
    for (i = 0; i < 256; i++)
        buf[i] = 0;
    *(DWORD *)buf = 242;                /* NEWCPLINFO's dwSize */
    clear();
    args[10] = 0;
    args[11] = 8;
    args[14] = SELECTOROF((void FAR *)buf);
    args[15] = OFFSETOF((void FAR *)buf);
    r = call(p[6]);
    report("CplApplet NEWINQUIRE", r);
    obs("CplApplet NEWINQUIRE DX", HIWORD(r));
    bdump("NEWCPLINFO", buf, 20);
    for (i = 0; i < 16; i++)
        buf[i] = 0;
    args[11] = 3;
    r = call(p[6]);
    report("CplApplet INQUIRE", r);
    obs("CplApplet INQUIRE DX", HIWORD(r));
    bdump("CPLINFO", buf, 10);

    /* the event procedure */
    sel = (WORD)GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, D_REC + MAXREC * 16L);
    obs("block", sel != 0);
    if (!sel)
        return 1;
    w = (WORD FAR *)MAKELP(sel, 0);
    for (i = 0; i < md_len; i++)
        ((BYTE FAR *)w)[H_OFF + i] = md_code[i];
    ume = GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(299));
    w[D_SEL / 2] = sel;
    w[D_MAX / 2] = MAXREC;
    w[D_NEXT / 2] = OFFSETOF(ume);
    w[D_NEXT / 2 + 1] = SELECTOROF(ume);
    csel = AllocDStoCSAlias(sel);
    obs("code alias", csel != 0);
    clear();
    r = call(p[3]);
    report("~Disable", r);
    clear();
    args[14] = csel;
    args[15] = H_OFF;
    r = call(p[2]);
    report("~Enable", r);
    out("listening\r\n");
    secs = GetPrivateProfileInt("w16", "md1secs", 50, "SYSTEM.INI");
    wait(secs);
    clear();
    call(p[3]);
    clear();
    args[14] = SELECTOROF(ume);
    args[15] = OFFSETOF(ume);
    call(p[2]);
    out("USER's event procedure back\r\n");

    n = w[D_N / 2];
    obs("~events", n);
    for (i = 0; i < 16; i++)
        cnt[i] = 0;
    for (i = 0; i < n; i++) {
        WORD FAR *e = w + (D_REC + i * 16) / 2;
        WORD b;
        wsprintf(line, "E %04X %d %d %u %04X %04X IF=%u\r\n", e[0], (int)e[1], (int)e[2],
                 e[3], e[4], e[5], (e[6] >> 9) & 1);
        out(line);
        for (b = 0; b < 16; b++)
            if (e[0] & (1 << b))
                cnt[b]++;
        if (e[0] & 1) {
            sx += (int)e[1];
            sy += (int)e[2];
            moves++;
        }
        if (e[0] & ~0x1F)
            other |= e[0] & ~0x1F;
        dxs |= 1 << (e[3] & 15);
        si |= e[4];
        di |= e[5];
        ifs |= 1 << ((e[6] >> 9) & 1);
    }
    wsprintf(line, "OBS ~motion events=%u x total=%ld y total=%ld\r\n", moves, sx, sy);
    out(line);
    obs("SF_B1_DOWN", cnt[1]);
    obs("SF_B1_UP", cnt[2]);
    obs("SF_B2_DOWN", cnt[3]);
    obs("SF_B2_UP", cnt[4]);
    obs("other status bits", other);
    obs("DX values (bit n: n buttons)", dxs);
    obs("SI ored", si);
    obs("DI ored", di);
    obs("IF values (bit 0: clear, bit 1: set)", ifs);
    obs("deepest nesting", w[D_NEST / 2]);

    /* the two whose purpose is unknown: bytes taken off the stack */
    out("calling GetSetMouseData\r\n");
    clear();
    r = call(p[5]);
    report("GetSetMouseData", r);
    obs("GetSetMouseData DX", HIWORD(r));
    out("calling PowerEventProc\r\n");
    clear();
    r = call(p[7]);
    report("PowerEventProc", r);
    obs("PowerEventProc DX", HIWORD(r));
    /* MS1 or MS2 (tests/dos/ms2.asm) loaded before WIN: its log too
       (INT 66h points at the header; records as TK49 prints them) */
    {
        WORD ivtsel = (WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0000H");
        WORD FAR *ivt = (WORD FAR *)MAKELP(ivtsel, 0);
        WORD off = ivt[0x66 * 2], seg = ivt[0x66 * 2 + 1];
        WORD ls = AllocSelector(SELECTOROF((void FAR *)&i));
        WORD FAR *h;
        SetSelectorBase(ls, (DWORD)seg << 4);
        SetSelectorLimit(ls, 0xFFFF);
        h = (WORD FAR *)MAKELP(ls, off);
        if (seg && h[1] == 1500) {
            n = h[0];
            for (i = 0; i < n; i++) {
                WORD FAR *e = (WORD FAR *)MAKELP(ls, h[10] + i * 16);
                wsprintf(line, "OBS ~R %u %04X %04X %04X %04X #%u\r\n",
                         e[0], e[1], e[2], e[3], e[4], e[7]);
                out(line);
            }
        }
        FreeSelector(ls);
    }
    out("MD1 done\r\n");
    return 0;
}
