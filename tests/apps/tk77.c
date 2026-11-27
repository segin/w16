/*
 * tk77.c - ExitWindowsExec: what program WIN.COM runs, with which
 * command tail, and that Windows comes back. Run as the shell
 * (boot-test case exitexec), with TK77D.COM (tests/dos/tk77d.asm), a DOS
 * program that appends its command tail to C:\TK77.LOG, in C:\ and in
 * the Windows directory. Each boot is one round, told by the number of
 * "D" lines TK77D has written so far:
 *   0  ExitWindowsExec("C:\TK77D.COM", "one  two/x") (a full path,
 *      parameters with two blanks and a switch)
 *   1  ExitWindowsExec("TK77D.COM", "") (a bare name: WIN.COM's own
 *      directory, the Windows directory; no parameters)
 *   2  ExitWindowsExec("C:\TK77D.COM", NULL)
 *   3  done: ExitWindows(0), and C:\TK77.END for the test to stop.
 * A round whose ExitWindowsExec returns logs "T n returned v". TK77's
 * own (hidden) top-level window logs WM_QUERYENDSESSION and WM_ENDSESSION
 * ("W" lines): on 3.1 ExitWindows sends them to the other tasks' windows
 * only (Pietrek, Windows Internals ch. 1, QueryQuitEnum: "Don't bother
 * with windows in the current task/queue"). Before
 * each call TK77 fills the first 128 bytes of WIN.COM's buffer (INT 2Fh
 * AX=4B20h, DX:CX) with EEh, and at the start of each later round logs
 * them ("B" lines, hex): what USER wrote there, and what WIN.COM left.
 * Microsoft's Setup toolkit (Win32s 1.30c's Setup) restarts Windows this
 * way at its end; w16's USER had no ExitWindowsExec and its KERNEL ended
 * with 0 instead of ExitKernel's 44h, so Windows never came back.
 * Writes C:\TK77.LOG.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];       /* InitTask's ax bx cx dx si di es */
extern BOOL FAR PASCAL INITAPP(HINSTANCE inst);                 /* USER.5 */
extern BOOL FAR PASCAL EXITWINDOWSEXEC(LPCSTR exe, LPCSTR params); /* USER.246 */

static char buf[2048];

static WORD r_ax, r_dx, r_cx;           /* INT 2Fh 4B20h's answer */
static void win_buf(void);
#pragma aux win_buf = \
    "mov ax, 4B20h" "xor cx, cx" "xor dx, dx" "int 2Fh" \
    "mov r_ax, ax" "mov r_dx, dx" "mov r_cx, cx" \
    modify [ax bx cx dx si di es];
static WORD seg_sel(WORD seg);          /* DPMI 0002h */
#pragma aux seg_sel = \
    "mov ax, 0002h" "int 31h" "jnc l1" "xor ax, ax" "l1:" \
    parm [bx] value [ax] modify [ax];

/* WIN.COM's buffer: at a later round, its first 128 bytes as "B" lines
   (hex); then filled with EEh, so that the next dump shows what was
   written */
static void put(const char *s);
static void buffer(int r)
{
    WORD sel, i, j;
    BYTE __far *w;
    char h[70];

    win_buf();
    h[0] = 'R', h[1] = ' ';
    for (i = 0; i < 1; i++) {
        WORD v = i == 0 ? r_ax : i == 1 ? r_dx : r_cx;
        for (j = 0; j < 4; j++)
            h[2 + 5 * i + j] = "0123456789ABCDEF"[(v >> (12 - 4 * j)) & 15];
        h[6 + 5 * i] = ' ';
    }
    h[7] = '\r', h[8] = '\n', h[9] = 0;
    put(h);
    sel = r_ax == 0 && r_dx ? seg_sel(r_dx) : 0;
    if (!sel) {
        put("B none\r\n");
        return;
    }
    w = (BYTE __far *)MAKELP(sel, r_cx);
    for (i = 0; r && i < 256; i += 32) {
        h[0] = 'B', h[1] = ' ';
        for (j = 0; j < 32; j++) {
            h[2 + 2 * j] = "0123456789ABCDEF"[w[i + j] >> 4];
            h[3 + 2 * j] = "0123456789ABCDEF"[w[i + j] & 15];
        }
        h[66] = '\r', h[67] = '\n', h[68] = 0;
        put(h);
    }
    for (i = 0; i < 128; i++)
        w[i] = 0xEE;
}

static void put(const char *s)
{
    HFILE f = _lopen("C:\\TK77.LOG", OF_READWRITE);
    int n = 0;
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\TK77.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    while (s[n])
        n++;
    _lwrite(f, s, n);
    _lclose(f);
}

static int starts;              /* "T n start" lines so far */

/* the number of lines of TK77D's ("D ...") in the log */
static int rounds(void)
{
    HFILE f = _lopen("C:\\TK77.LOG", OF_READ);
    int n, i, r = 0;
    if (f == HFILE_ERROR)
        return 0;
    n = _lread(f, buf, sizeof buf - 1);
    _lclose(f);
    if (n <= 0)
        return 0;
    for (i = 0; i < n; i++)
        if (i == 0 || buf[i - 1] == '\n') {
            if (buf[i] == 'D')
                r++;
            else if (buf[i] == 'T' && buf[i + 2] != 'd')
                starts++;
        }
    return r;
}

/* TK77's own top-level window: whether ExitWindows asks it
   (WM_QUERYENDSESSION, "W q") and tells it (WM_ENDSESSION, "W e") */
LRESULT FAR PASCAL __export __loadds TK77_WND(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_QUERYENDSESSION) {
        put("W q\r\n");
        return TRUE;
    }
    if (m == WM_ENDSESSION) {
        put(wp ? "W e 1\r\n" : "W e 0\r\n");
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

static void own_window(void)
{
    WNDCLASS wc;
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)TK77_WND;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = 0;
    wc.lpszClassName = "TK77";
    RegisterClass(&wc);
    CreateWindow("TK77", "TK77", WS_OVERLAPPED, 0, 0, 100, 50, 0, 0, hi, 0);
}

int __cdecl tmain(void)
{
    int r;
    BOOL v;
    char line[40];

    INITAPP((HINSTANCE)init_regs[5]);
    r = rounds();
    if (starts >= 6) {          /* a program that never ran: stop */
        put("T stuck\r\n");
        _lclose(_lcreat("C:\\TK77.END", 0));
        ExitWindows(0, 0);
        return 0;
    }
    line[0] = 'T', line[1] = ' ', line[2] = (char)('0' + r);
    lstrcpy(line + 3, " start\r\n");
    put(line);
    buffer(r);
    own_window();
    switch (r) {
    case 0: v = EXITWINDOWSEXEC("C:\\TK77D.COM", "one  two/x"); break;
    case 1: v = EXITWINDOWSEXEC("TK77D.COM", ""); break;
    case 2: v = EXITWINDOWSEXEC("C:\\TK77D.COM", NULL); break;
    default:
        put("T done\r\n");
        _lclose(_lcreat("C:\\TK77.END", 0));
        ExitWindows(0, 0);
        return 0;
    }
    lstrcpy(line + 3, " returned ");
    line[13] = (char)('0' + (v != 0));
    lstrcpy(line + 14, "\r\n");
    put(line);
    _lclose(_lcreat("C:\\TK77.END", 0));
    ExitWindows(0, 0);
    return 0;
}
