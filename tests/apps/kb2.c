/*
 * kb2.c - KB2: KEYBOARD.DRV under real key input (drivers/keyboard/
 * README.md). Run as the shell under tests/inputrun.sh with KBREC.DRV (a
 * recording KEYBOARD in front of KBTARGET, the driver under test) and KB4.COM
 * (a real-mode TSR recording INT 9, INT 16h and INT 15h AH=4Fh) loaded;
 * tests/kbprobe.sh runs it with Windows 3.1's KEYBOARD.DRV and with
 * w16's and compares.
 *
 * KB2 opens a window with the focus and logs every keyboard message it
 * gets (TranslateMessage on, so USER calls the driver's ToAscii) while
 * the input script types, until Shift+F12 (or [w16] kb2secs seconds,
 * default 300); then
 * it writes out KBREC's records (every call USER and the display driver
 * made to the driver, every call the driver made to USER's event
 * procedure) and KB4's (what reached real mode), and ends (so Windows
 * ends: KB2 is the shell).
 *
 * Output lines (TK.LOG):
 *   M msg wParam lParam                  a message (WM_KEYDOWN etc.)
 *   E AX BX IF= 17= 96= depth=           an event-procedure call
 *   ~E SI DI CX DX DS ES SS caller       its other registers (Windows 3.1's
 *                                        driver leaves SI, DI, CX, DX as
 *                                        they happen to be)
 *   C name args -> AX [DX] popped=       a call to the driver
 *   ~C ...                               the raw record
 *   R kind w1 w2 w3 w4 w5                a real-mode record (KB4)
 *   S xx xx ...                          the bytes the keyboard controller
 *                                        gave (port 60h), as an INT 9
 *                                        handler of KB2's in front of the
 *                                        driver's read them (kb2s.asm)
 */
#include <windows.h>

#define TK_REF "kb1ref.h"
#include "tklog.h"

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern BYTE __far __cdecl sc_code[];
extern WORD __far __cdecl sc_len;
extern void __cdecl __far get_vec9(WORD FAR *v);
extern void __cdecl __far set_vec9(WORD sel, WORD off);

/* the scan-code logger (kb2s.asm): a fixed block, its code alias */
#define SC_MAX 4000
static WORD scsel, sccs, old9[2];

static void sc_install(void)
{
    WORD FAR *w;
    WORD i;
    scsel = (WORD)GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 256 + SC_MAX);
    if (!scsel)
        return;
    w = (WORD FAR *)MAKELP(scsel, 0);
    for (i = 0; i < sc_len; i++)
        ((BYTE FAR *)w)[16 + i] = sc_code[i];
    get_vec9(old9);
    w[0] = scsel;
    w[2] = SC_MAX;
    w[3] = old9[1];
    w[4] = old9[0];
    sccs = AllocDStoCSAlias(scsel);
    if (sccs)
        set_vec9(sccs, 16);
}

static void sc_remove(void)
{
    if (sccs)
        set_vec9(old9[0], old9[1]);
}

static char lb[4096];
static WORD lbn;
static char line[200];
static WORD FAR *msgs;          /* 4 words each: msg, wParam, lParam */
static WORD nmsg, maxmsg = 4000;
static WORD ticks;
static int done;

static void flush(void)
{
    if (lbn)
        log_write(lb, lbn);
    lbn = 0;
}

static void put(const char FAR *s)
{
    while (*s) {
        if (lbn == sizeof lb)
            flush();
        lb[lbn++] = *s++;
    }
}

LRESULT FAR PASCAL __export __loadds kb2proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_DEADCHAR:
    case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_SYSCHAR: case WM_SYSDEADCHAR:
        if (nmsg < maxmsg) {
            WORD FAR *m = msgs + nmsg * 4;
            m[0] = msg;
            m[1] = wp;
            m[2] = LOWORD(lp);
            m[3] = HIWORD(lp);
            nmsg++;
        }
        /* the end: Shift+F12 (DOSBox keeps Ctrl+F12) */
        if (msg == WM_KEYDOWN && wp == VK_F12 && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_SHIFT) < 0)
            done = 1;
        return 0;                       /* no menus, no task switching */
    case WM_TIMER:
        ticks++;
        return 0;
    case WM_SYSCOMMAND:
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static const char *ordname(WORD o)
{
    switch (o) {
    case 1: return "Inquire";
    case 2: return "Enable";
    case 3: return "Disable";
    case 4: return "ToAscii";
    case 5: return "AnsiToOem";
    case 6: return "OemToAnsi";
    case 7: return "SetSpeed";
    case 100: return "ScreenSwitchEnable";
    case 126: return "GetTableSeg";
    case 127: return "NewTable";
    case 128: return "OemKeyScan";
    case 129: return "VkKeyScan";
    case 130: return "GetKeyboardType";
    case 131: return "MapVirtualKey";
    case 132: return "GetKBCodePage";
    case 133: return "GetKeyNameText";
    case 134: return "AnsiToOemBuff";
    case 135: return "OemToAnsiBuff";
    case 136: return "EnableKBSysReq";
    case 137: return "GetBIOSKeyProc";
    }
    return "?";
}

static void dump_kbrec(void)
{
    FARPROC f = GetProcAddress(GetModuleHandle("KEYBOARD"), MAKEINTRESOURCE(200));
    WORD FAR *h, FAR *r;
    WORD i, n, sz;
    if (!f) {
        put("no KBREC\r\n");
        return;
    }
    {
        DWORD a = ((DWORD (FAR PASCAL *)(void))f)();
        h = (WORD FAR *)a;
    }
    n = h[0];
    sz = h[2];
    wsprintf(line, "KBREC records=%u total=%u\r\n", n, h[3]);
    put(line);
    for (i = 0; i < n; i++) {
        r = (WORD FAR *)((BYTE FAR *)h + 16 + i * sz);
        if (r[0] == 0xE0) {
            wsprintf(line, "E %04X %04X IF=%u 17=%04X 96=%04X depth=%u\r\n",
                     r[2], r[3], (r[8] >> 9) & 1, r[12], r[13], r[14]);
            put(line);
            wsprintf(line, "~E SI=%04X DI=%04X CX=%04X DX=%04X DS=%04X ES=%04X SS=%04X flags=%04X from %04X:%04X tick=%04X #%u r=%u\r\n",
                     r[6], r[7], r[4], r[5], r[9], r[10], r[11], r[8], r[17], r[16], r[15], r[1], r[18]);
            put(line);
        } else {
            WORD o = r[0];
            WORD FAR *w = r + 12;
            switch (o) {
            case 4:
                wsprintf(line, "C ToAscii vk=%02X scan=%04X flags=%04X -> %04X popped=%u\r\n",
                         w[6], w[5], w[0], r[20], r[23]);
                break;
            case 7: case 100: case 130: case 136:
                wsprintf(line, "C %s(%04X) -> %04X popped=%u\r\n", (LPSTR)ordname(o), w[0], r[20], r[23]);
                break;
            case 128: case 129:
                wsprintf(line, "C %s(%04X) -> %04X %04X popped=%u\r\n", (LPSTR)ordname(o), w[0], r[20], r[21],
                         r[23]);
                break;
            case 131:
                wsprintf(line, "C MapVirtualKey(%04X, %u) -> %04X popped=%u\r\n", w[1], w[0], r[20], r[23]);
                break;
            case 134: case 135:
                wsprintf(line, "C %s(n=%u) popped=%u\r\n", (LPSTR)ordname(o), w[0], r[23]);
                break;
            case 1: case 5: case 6: case 132: case 133:
                wsprintf(line, "C %s -> %04X popped=%u\r\n", (LPSTR)ordname(o), r[20], r[23]);
                break;
            default:
                wsprintf(line, "C %s popped=%u\r\n", (LPSTR)ordname(o), r[23]);
                break;
            }
            put(line);
            if (r[25] != r[24]) {
                wsprintf(line, "C   real-mode records %u\r\n", r[25] - r[24]);
                put(line);
            }
            wsprintf(line, "~C %u #%u AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X F=%04X DS=%04X ES=%04X"
                     " w=%04X %04X %04X %04X %04X %04X %04X %04X -> %04X %04X F=%04X\r\n",
                     o, r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10],
                     w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], r[20], r[21], r[22]);
            put(line);
        }
        if ((i & 15) == 15)
            flush();
    }
}

static void dump_kb4(void)
{
    WORD ivtsel = (WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0000H");
    WORD FAR *ivt = (WORD FAR *)MAKELP(ivtsel, 0);
    WORD off = ivt[0x65 * 2], seg = ivt[0x65 * 2 + 1];
    WORD ls, i, n;
    WORD FAR *h;
    if (!seg) {
        put("no KB4\r\n");
        return;
    }
    ls = AllocSelector(SELECTOROF((void FAR *)&i));
    SetSelectorBase(ls, (DWORD)seg << 4);
    SetSelectorLimit(ls, 0xFFFF);
    h = (WORD FAR *)MAKELP(ls, off);
    if (h[2] == 0x344B) {               /* 'K4' */
        n = h[0];
        wsprintf(line, "KB4 records=%u seq=%u\r\n", n, h[3]);
        put(line);
        for (i = 0; i < n; i++) {
            WORD FAR *e = (WORD FAR *)MAKELP(ls, h[10] + i * 16);
            wsprintf(line, "R %u %04X %04X %04X %04X %04X ~tick=%04X #%u\r\n",
                     e[0], e[1], e[2], e[3], e[4], e[5], e[6], e[7]);
            put(line);
            if ((i & 15) == 15)
                flush();
        }
    } else
        put("no KB4 header\r\n");
    FreeSelector(ls);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HWND hwnd;
    MSG m;
    WORD secs, i;
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    U_INITAPP(init_regs[5]);
    out("KB2 start\r\n");
    msgs = (WORD FAR *)GlobalLock(GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, (DWORD)maxmsg * 8));
    wc.style = 0;
    wc.lpfnWndProc = kb2proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = "KB2";
    RegisterClass(&wc);
    hwnd = CreateWindow("KB2", "KB2", WS_POPUP | WS_CAPTION | WS_VISIBLE, 40, 40, 400, 300, 0, 0, hi, 0);
    SetFocus(hwnd);
    sc_install();
    secs = GetPrivateProfileInt("w16", "kb2secs", 300, "SYSTEM.INI");
    SetTimer(hwnd, 1, 1000, NULL);
    out("listening\r\n");
    while (!done && ticks < secs && GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    KillTimer(hwnd, 1);
    sc_remove();
    wsprintf(line, "messages=%u\r\n", nmsg);
    put(line);
    for (i = 0; i < nmsg; i++) {
        WORD FAR *e = msgs + i * 4;
        wsprintf(line, "M %04X %04X %04X%04X\r\n", e[0], e[1], e[3], e[2]);
        put(line);
        if ((i & 31) == 31)
            flush();
    }
    {
        BYTE ks[256];
        GetKeyboardState(ks);
        put("keystate:");
        for (i = 0; i < 256; i++)
            if (ks[i]) {
                wsprintf(line, " %02X=%02X", i, ks[i]);
                put(line);
            }
        put("\r\n");
    }
    if (sccs) {
        WORD FAR *w = (WORD FAR *)MAKELP(scsel, 0);
        BYTE FAR *b = (BYTE FAR *)w + 256;
        wsprintf(line, "scan codes=%u", w[1]);
        put(line);
        for (i = 0; i < w[1]; i++) {
            wsprintf(line, "%s%02X", (LPSTR)(i % 32 ? " " : "\r\nS "), b[i]);
            put(line);
        }
        put("\r\n");
    }
    dump_kbrec();
    dump_kb4();
    put("KB2 done\r\n");
    flush();
    DestroyWindow(hwnd);
    return 0;
}
