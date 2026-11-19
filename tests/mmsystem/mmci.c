/*
 * mmci.c - MMP.EXE's MCI section (layer 4; tests/mmsystem/mmp.c,
 * ARCHITECTURE.md 23): section Q runs the script C:\MCI.TXT, one command
 * per line, and logs what MMSYSTEM answers; with MMSYSLOG.DLL loaded the
 * records of every call that reached an MCI driver (MCT.DRV, the probe's
 * own test driver, tests/mmsystem/mct.c, or a Microsoft driver behind a
 * logging pass-through driver MDSC*.DRV) follow each line ("  MD ...",
 * "  FWD ..."), then the messages the probe's window got
 * (MM_MCINOTIFY). The same script runs on Microsoft's MMSYSTEM and on
 * w16's, and the logs are compared (tests/mmsystem/mcmp.py).
 *
 * Script lines (blanks separate; NUMBERS hexadecimal unless said):
 *   # ...             a comment
 *   : TEXT            "== TEXT" in the log
 *   s[/rN][/cH] CMD   mciSendString(CMD, a 200-byte buffer, 200, the
 *                     probe's window); /rN: N bytes (decimal; /r-1 a NULL
 *                     buffer of size 200, /r-2 NULL and 0); /cH: callback
 *                     H instead of the window (/c0 none)
 *   e N [M]           mciGetErrorString(N) (decimal), each number to M;
 *                     a run with the same answer is one line
 *   E N SIZE          mciGetErrorString(N, a buffer, SIZE): the bytes
 *   i NAME            mciGetDeviceID(NAME); i/ ELEMENTID TYPE:
 *                     mciGetDeviceIDFromElementID
 *   c DEV MSG FLAGS A...  mciSendCommand(DEV, MSG, FLAGS, a block of
 *                     DWORDs A...): a number, s:TEXT (a pointer to TEXT;
 *                     ~ is a blank), b:N (a pointer to N bytes of 'x',
 *                     shown after), w (the probe's window), $K (device
 *                     slot K); "-" as the only A: a NULL block; =K at
 *                     the end keeps the block's WORD at 4 (an opened
 *                     device's ID) in slot K; DEV may be $K
 *   x CMD             mciExecute(CMD); the message box it shows (caption
 *                     and text) is logged and closed
 *   t WHAT V          MCT.DRV's MctSet(WHAT, V); T TEXT: MctStr(TEXT)
 *   w MS              messages for MS milliseconds (decimal)
 *   y DEV ...         yield procedures (mmci.c's yieldtests: g s n r c, q the
 *                     procedure with messages queued, Y whether it and
 *                     mmTaskYield let another task run)
 *   d DEV             mciGetCreatorTask, mciGetDriverData,
 *                     mciSetDriverData from the application
 *   l HMOD NAME TYPE  mciLoadCommandResource(the module HMOD names, NAME,
 *                     TYPE decimal); f N: mciFreeCommandResource(N)
 *   p N               the next N lines run with the call's result only
 *                     (no records)
 *
 * Sources: the Multimedia Programmer's Guide ch. 2 and 4 (the MCI
 * functions, commands, structures, flags and MM_MCINOTIFY); the probe
 * only calls them.
 */
#include <windows.h>
#include <mmsystem.h>
#include <toolhelp.h>
#include "mmc.h"

extern void lg(const char *fmt, ...);
extern void put(const char FAR *s, int n);
extern char line[600];
extern HWND dev_hwnd;

typedef void (FAR PASCAL *MLDUMP)(LPCSTR);
typedef void (FAR PASCAL *MLNAME)(DWORD, LPCSTR);
typedef void (FAR PASCAL *MLPROBE)(WORD, HTASK);
typedef void (FAR PASCAL *MCTSET)(WORD, DWORD);
typedef void (FAR PASCAL *MCTSTR)(LPCSTR);
static MLDUMP mldump;
static MLNAME mlname;

extern WORD getss3(void);
#pragma aux getss3 = "mov ax, ss" value [ax];

extern WORD FAR PASCAL K_GETEXEPTR(WORD h);     /* KERNEL.133 */
/* MMSYSTEM's MCI functions OpenWatcom's mmsystem.h does not declare
   (imported by ordinal, tests/mmsystem/mmp.lnk) */
extern UINT FAR PASCAL MM_GETDEVICEIDFROMELEMENTID(DWORD, LPCSTR);  /* .715 */
extern UINT FAR PASCAL MM_LOADCOMMANDRESOURCE(HINSTANCE, LPCSTR, UINT); /* .705 */
extern BOOL FAR PASCAL MM_SETDRIVERDATA(UINT, DWORD);       /* .707 */
extern DWORD FAR PASCAL MM_GETDRIVERDATA(UINT);             /* .708 */
extern BOOL FAR PASCAL MM_EXECUTE(LPCSTR);                  /* .712 */
extern BOOL FAR PASCAL MM_FREECOMMANDRESOURCE(UINT);        /* .713 */

static WORD slots[10];
static char buf[260];
static BYTE bufs[4][130];

static const char *modname(WORD sel)
{
    static char b[4][20];
    static int k;
    WORD m = sel ? K_GETEXEPTR(sel) : 0;
    MODULEENTRY me;
    char *r = b[k++ & 3];
    if (!m)
        return "0";
    me.dwSize = sizeof me;
    if (!ModuleFindHandle(&me, (HMODULE)m))
        return "?";
    lstrcpy(r, me.szModule);
    return r;
}

/* the records since the last, and the messages that came */
static void flush(void)
{
    MSG m;
    if (mldump)
        mldump("C:\\MMP.LOG");
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
        if (m.message == MM_MCINOTIFY)
            lg("  MM_MCINOTIFY%s wParam %04X lParam %08lX", (LPSTR)(m.hwnd == dev_hwnd ? "" :
               m.hwnd ? " (another window)" : " (task)"), m.wParam, m.lParam);
        else if (!m.hwnd)
            lg("  task message %04X wParam %04X lParam %08lX", m.message, m.wParam, m.lParam);
        else if (m.hwnd == dev_hwnd && m.message >= WM_USER)
            lg("  window message %04X wParam %04X lParam %08lX", m.message, m.wParam, m.lParam);
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

static void waitms(DWORD ms)
{
    DWORD t = GetTickCount();
    while (GetTickCount() - t < ms) {
        flush();
        Yield();
    }
    flush();
}

static DWORD hexnum(const char **pp)
{
    const char *p = *pp;
    DWORD v = 0;
    int neg = 0;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    for (;; p++) {
        char c = *p;
        if (c >= '0' && c <= '9')
            v = v * 16 + (c - '0');
        else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
            v = v * 16 + ((c | 0x20) - 'a' + 10);
        else
            break;
    }
    *pp = p;
    return neg ? (DWORD)-(long)v : v;
}

static long decnum(const char **pp)
{
    const char *p = *pp;
    long v = 0;
    int neg = 0;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    *pp = p;
    return neg ? -v : v;
}

static const char *skip(const char *p)
{
    while (*p == ' ')
        p++;
    return p;
}

/* a returned string: its text, and how many of the 200 bytes changed */
static char *showbuf(char *p, const char *b, int size)
{
    int n, i;
    if (size <= 0)
        return p;
    for (n = size; n > 0 && b[n - 1] == 'x'; n--)
        ;
    if (!n)
        return p + wsprintf(p, " (untouched)");
    p += wsprintf(p, " \"");
    for (i = 0; i < n && b[i]; i++)
        *p++ = b[i] >= ' ' ? b[i] : '?';
    p += wsprintf(p, "\"");
    if (i < n - 1)
        p += wsprintf(p, " (+%d bytes)", n - 1 - i);
    *p = 0;
    return p;
}

static void fill(void FAR *d, int n, BYTE c)
{
    BYTE FAR *q = d;
    while (n--)
        *q++ = c;
}

/* ---- s: mciSendString ---- */
static void sendstring(const char *a)
{
    int size = 200;
    HWND cb = dev_hwnd;
    LPSTR rb = buf;
    DWORD r;
    char *p;
    while (*a == '/') {
        a++;
        if (*a == 'r') {
            a++;
            size = (int)decnum(&a);
        } else if (*a == 'c') {
            a++;
            cb = (HWND)(WORD)hexnum(&a);
        }
    }
    a = skip(a);
    fill(buf, sizeof buf, 'x');
    if (size == -1) {
        rb = NULL;
        size = 200;
    } else if (size == -2) {
        rb = NULL;
        size = 0;
    }
    r = mciSendString(a, rb, size, cb);
    p = line;
    p += wsprintf(p, "s \"%s\" = %lu", (LPSTR)a, r);
    if (rb)
        p = showbuf(p, buf, 200);
    lg("%s", (LPSTR)line);
    flush();
}

/* ---- e: mciGetErrorString ---- */
static void errstrings(const char *a)
{
    static char b[300], pb[300];
    DWORD e, to;
    UINT r, last = 0xFFFF;
    DWORD from = 0;
    e = (DWORD)decnum(&a);
    a = skip(a);
    to = *a ? (DWORD)decnum(&a) : e;
    for (; e <= to + 1; e++) {
        int end = e == to + 1;
        if (!end) {
            fill(b, sizeof b, 'x');
            b[299] = 0;
            r = mciGetErrorString(e, b, 299);
        }
        if (last != 0xFFFF && (end || r != last || lstrcmp(b, pb))) {
            if (from == e - 1)
                lg("mciGetErrorString(%lu) = %u \"%s\"", from, last, (LPSTR)pb);
            else
                lg("mciGetErrorString(%lu-%lu) = %u \"%s\"", from, e - 1, last, (LPSTR)pb);
            last = 0xFFFF;
        }
        if (end)
            break;
        if (last == 0xFFFF) {
            from = e;
            last = r;
            lstrcpy(pb, b);
        }
    }
    flush();
}

static void errsize(const char *a)
{
    DWORD e = (DWORD)decnum(&a);
    int size, i;
    UINT r;
    char *p;
    a = skip(a);
    size = (int)decnum(&a);
    fill(buf, sizeof buf, 'x');
    r = mciGetErrorString(e, size < 0 ? NULL : buf, size < 0 ? 128 : size);
    p = line;
    p += wsprintf(p, "mciGetErrorString(%lu, size %d) = %u:", e, size, r);
    for (i = 0; i < size + 2 && i < 80; i++)
        p += wsprintf(p, " %02X", (BYTE)buf[i]);
    lg("%s", (LPSTR)line);
}

/* ---- c: mciSendCommand ---- */
/* a DWORD of a block: the probe's strings, buffers and window by name */
static char *blkval(char *p, DWORD v, char strs[][40], int nstr)
{
    int k;
    for (k = 0; k < nstr; k++)
        if (v == (DWORD)(LPSTR)strs[k])
            return p + wsprintf(p, "s%d", k);
    for (k = 0; k < 4; k++)
        if (v == (DWORD)(LPSTR)bufs[k])
            return p + wsprintf(p, "b%d", k);
    if (v == (DWORD)(LPSTR)buf)
        return p + wsprintf(p, "rbuf");
    if (v == 0xEEEEEEEEL)
        return p + wsprintf(p, "-");
    return p + wsprintf(p, "%lX", v);
}

static void sendcommand(const char *a)
{
    DWORD blk[16], before[16];
    int n = 0, nb = 0, keep = -1, isnull = 0, k;
    WORD dev, msg;
    DWORD flags, r;
    char *p;
    char strs[8][40];
    int nstr = 0;
    if (*a == '$') {
        a++;
        dev = slots[(int)decnum(&a) % 10];
    } else
        dev = (WORD)hexnum(&a);
    a = skip(a);
    msg = (WORD)hexnum(&a);
    a = skip(a);
    flags = hexnum(&a);
    for (k = 0; k < 16; k++)
        blk[k] = 0xEEEEEEEEL;
    for (;;) {
        a = skip(a);
        if (!*a)
            break;
        if (*a == '=') {
            a++;
            keep = (int)decnum(&a) % 10;
        } else if (*a == '-' && (a[1] == ' ' || !a[1])) {
            isnull = 1;
            a++;
        } else if (a[0] == 's' && a[1] == ':') {
            int i = 0;
            a += 2;
            while (*a && *a != ' ' && i < 39) {
                strs[nstr][i++] = *a == '~' ? ' ' : *a;
                a++;
            }
            strs[nstr][i] = 0;
            blk[n++] = (DWORD)(LPSTR)strs[nstr];
            nstr++;
        } else if (a[0] == 'b' && a[1] == ':') {
            int sz;
            a += 2;
            sz = (int)decnum(&a);
            fill(bufs[nb], sizeof bufs[nb], 'x');
            blk[n++] = (DWORD)(LPSTR)bufs[nb];
            nb++;
            (void)sz;
        } else if (*a == 'w' && (a[1] == ' ' || !a[1])) {
            blk[n++] = (DWORD)(WORD)dev_hwnd;
            a++;
        } else if (*a == '$') {
            a++;
            blk[n++] = slots[(int)decnum(&a) % 10];
        } else
            blk[n++] = hexnum(&a);
        if (n >= 15)
            break;
    }
    for (k = 0; k < 16; k++)
        before[k] = blk[k];
    r = mciSendCommand(dev, msg, flags, isnull ? 0L : (DWORD)(LPVOID)blk);
    p = line;
    p += wsprintf(p, "c %04X %04X %08lX = %lu (%04X)", dev, msg, flags, r, LOWORD(r));
    if (!isnull) {
        int last = n;
        for (k = n; k < 16; k++)
            if (blk[k] != before[k])
                last = k + 1;
        p += wsprintf(p, " block:");
        for (k = 0; k < last; k++) {
            p += wsprintf(p, " ");
            p = blkval(p, before[k], strs, nstr);
            if (blk[k] != before[k]) {
                p += wsprintf(p, "->");
                p = blkval(p, blk[k], strs, nstr);
            }
        }
    }
    lg("%s", (LPSTR)line);
    for (k = 0; k < nb; k++) {
        p = line;
        p += wsprintf(p, "  buffer %d:", k);
        p = showbuf(p, (const char *)bufs[k], 128);
        lg("%s", (LPSTR)line);
    }
    if (keep >= 0)
        slots[keep] = LOWORD(blk[1]);
    flush();
}

/* ---- x: mciExecute and its message box ---- */
static int boxes;

void FAR PASCAL __export MciBoxTimer(HWND h, UINT msg, UINT id, DWORD t)
{
    HWND box = GetActiveWindow(), c, okbtn = 0;
    char cap[80], txt[200];
    (void)msg;
    (void)t;
    KillTimer(h, id);
    if (!box || box == dev_hwnd) {
        lg("  (no box)");
        return;
    }
    GetWindowText(box, cap, sizeof cap);
    lg("  box \"%s\" style %08lX%s owner %s", (LPSTR)cap, GetWindowLong(box, GWL_STYLE),
       (LPSTR)(GetSysModalWindow() == box ? " system modal" : ""),
       (LPSTR)(GetWindow(box, GW_OWNER) == dev_hwnd ? "the probe's window" :
               GetWindow(box, GW_OWNER) ? "another window" : "none"));
    for (c = GetWindow(box, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        char cls[20];
        HICON ic = 0;
        static const char *const icons[] = { "hand", "question", "exclamation", "asterisk" };
        static LPCSTR const ids[] = { IDI_HAND, IDI_QUESTION, IDI_EXCLAMATION, IDI_ASTERISK };
        int k;
        GetClassName(c, cls, sizeof cls);
        GetWindowText(c, txt, sizeof txt);
        if ((GetWindowLong(c, GWL_STYLE) & 0x0F) == SS_ICON && !lstrcmpi(cls, "Static"))
            ic = (HICON)(WORD)SendMessage(c, WM_USER + 1, 0, 0);       /* STM_GETICON */
        for (k = 0; k < 4 && ic; k++)
            if (ic == LoadIcon(NULL, ids[k]))
                break;
        lg("    %s %04X \"%s\"%s%s", (LPSTR)cls, GetWindowWord(c, GWW_ID), (LPSTR)txt,
           (LPSTR)(ic ? " icon " : ""), (LPSTR)(ic ? (k < 4 ? icons[k] : "other") : ""));
        if (!lstrcmpi(cls, "Button") && !okbtn)
            okbtn = c;
    }
    boxes++;
    if (okbtn)
        PostMessage(box, WM_COMMAND, GetWindowWord(okbtn, GWW_ID), MAKELONG(okbtn, BN_CLICKED));
    else
        PostMessage(box, WM_COMMAND, IDOK, 0);
}

static void execute(const char *a)
{
    static FARPROC tp;
    BOOL r;
    if (!tp)
        tp = MakeProcInstance((FARPROC)MciBoxTimer, (HINSTANCE)GetWindowWord(dev_hwnd, GWW_HINSTANCE));
    SetTimer(dev_hwnd, 77, 700, (TIMERPROC)tp);
    boxes = 0;
    r = MM_EXECUTE(a);
    KillTimer(dev_hwnd, 77);
    lg("x \"%s\" = %d (%d box)", (LPSTR)a, r, boxes);
    flush();
}

/* ---- t, T: MCT.DRV's settings ---- */
static void mctset(const char *a, int str)
{
    HMODULE m = GetModuleHandle("MCT");
    if (!m) {
        lg("t: MCT not loaded");
        return;
    }
    if (str)
        ((MCTSTR)GetProcAddress(m, "MCTSTR"))(a);
    else {
        WORD what = (WORD)hexnum(&a);
        a = skip(a);
        ((MCTSET)GetProcAddress(m, "MCTSET"))(what, hexnum(&a));
    }
}

/* ---- y: yield procedures ---- */
static UINT yret;
static int ycalls;

UINT FAR PASCAL __export MciYield(UINT dev, DWORD data)
{
    ycalls++;
    lg("  yield proc: device %u data %08lX (call %d) -> %u", dev, data, ycalls, yret);
    return yret;
}

static void yieldtests(const char *a)
{
    static FARPROC yp;
    WORD dev;
    DWORD data;
    YIELDPROC g;
    char op;
    if (!yp)
        yp = MakeProcInstance((FARPROC)MciYield, (HINSTANCE)GetWindowWord(dev_hwnd, GWW_HINSTANCE));
    if (*a == '$') {
        a++;
        dev = slots[(int)decnum(&a) % 10];
    } else
        dev = (WORD)hexnum(&a);
    a = skip(a);
    op = *a++;
    a = skip(a);
    switch (op) {
    case 'g':           /* mciGetYieldProc */
        data = 0xEEEEEEEEL;
        g = mciGetYieldProc(dev, &data);
        lg("y mciGetYieldProc(%u) = %s%s data %08lX", dev,
           (LPSTR)(g == (YIELDPROC)yp ? "the probe's" : g ? modname(SELECTOROF(g)) : "NULL"),
           (LPSTR)(g && g != (YIELDPROC)yp ? "'s" : ""), data);
        break;
    case 's':           /* mciSetYieldProc(the probe's, data) */
        data = hexnum(&a);
        lg("y mciSetYieldProc(%u, the probe's, %08lX) = %d", dev, data,
           mciSetYieldProc(dev, (YIELDPROC)yp, data));
        break;
    case 'n':           /* mciSetYieldProc(NULL, data) */
        data = hexnum(&a);
        lg("y mciSetYieldProc(%u, NULL, %08lX) = %d", dev, data, mciSetYieldProc(dev, NULL, data));
        break;
    case 'r':           /* what the probe's answers */
        yret = (UINT)hexnum(&a);
        ycalls = 0;
        break;
    case 'c': {         /* call the procedure mciGetYieldProc gives */
        g = mciGetYieldProc(dev, &data);
        if (g)
            lg("y calling it: %u", g(dev, data));
        else
            lg("y no procedure");
        break;
    }
    case 'q': {         /* the same with a window message and a task
                           message queued: what is left after it */
        MSG m;
        flush();
        PostMessage(dev_hwnd, WM_USER + 0x77, 1, 2);
        PostAppMessage(GetCurrentTask(), WM_USER + 0x78, 3, 4);
        g = mciGetYieldProc(dev, &data);
        if (g)
            lg("y calling it with two messages queued: %u", g(dev, data));
        else
            lg("y no procedure");
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
            lg("y left: %s message %04X", (LPSTR)(m.hwnd ? "window" : "task"), m.message);
        break;
    }
    case 'Y': {         /* does it let other tasks run, with and without a
                           message queued? (a task of MMC's counts its
                           turns) */
        typedef UINT (FAR PASCAL *TCREATE)(FARPROC, WORD FAR *, DWORD);
        TCREATE tc = (TCREATE)GetProcAddress(GetModuleHandle("MMSYSTEM"), MAKEINTRESOURCE(900));
        FARPROC fn = GetProcAddress(GetModuleHandle("MMC"), "MMCTASK");
        struct mmcstate FAR *st = ((MMCSTATEPROC)GetProcAddress(GetModuleHandle("MMC"), "MMCSTATE"))();
        typedef void (FAR PASCAL *TYIELD)(void);
        TYIELD ty = (TYIELD)GetProcAddress(GetModuleHandle("MMSYSTEM"), MAKEINTRESOURCE(905));
        WORD h = 0;
        DWORD c0;
        int k, q;
        MSG m;
        flush();
        st->taskstop = 0;
        st->taskdone = 0;
        st->taskcount = 0;
        g = mciGetYieldProc(dev, &data);
        if (!g || tc(fn, &h, 0x5949434EL)) {
            lg("y no procedure or no task");
            break;
        }
        for (k = 0; k < 20 && !st->taskcount; k++)
            Yield();
        for (q = 0; q < 4; q++) {
            if (q & 1)
                PostMessage(dev_hwnd, WM_USER + 0x77, 1, 2);
            c0 = st->taskcount;
            for (k = 0; k < 20; k++)
                if (q < 2)
                    g(dev, data);
                else
                    ty();
            lg("y 20 calls of %s, %s: the other task ran %s",
               (LPSTR)(q < 2 ? "the yield procedure" : "mmTaskYield"),
               (LPSTR)(q & 1 ? "a window message queued" : "nothing queued"),
               (LPSTR)(st->taskcount > c0 ? "meanwhile" : "not at all"));
            while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
                ;
        }
        st->taskstop = 1;
        for (k = 0; k < 50 && !st->taskdone; k++)
            Yield();
        break;
    }
    }
    flush();
}

/* ---- d: the driver helpers from the application ---- */
static void helpers(const char *a)
{
    WORD dev;
    HTASK t;
    if (*a == '$') {
        a++;
        dev = slots[(int)decnum(&a) % 10];
    } else
        dev = (WORD)hexnum(&a);
    t = mciGetCreatorTask(dev);
    lg("d mciGetCreatorTask(%u) = %s", dev, (LPSTR)(t == GetCurrentTask() ? "the probe's task" :
                                                   t ? "another task" : "0"));
    lg("d mciGetDriverData(%u) = %08lX", dev, MM_GETDRIVERDATA(dev));
    lg("d mciSetDriverData(%u, 12345678) = %d", dev, MM_SETDRIVERDATA(dev, 0x12345678L));
    lg("d mciGetDriverData(%u) = %08lX", dev, MM_GETDRIVERDATA(dev));
    flush();
}

/* ---- l, f: command tables ---- */
static void loadtable(const char *a)
{
    char mod[20], name[40];
    int i = 0;
    HINSTANCE h;
    UINT type, r;
    while (*a && *a != ' ' && i < 19)
        mod[i++] = *a++;
    mod[i] = 0;
    a = skip(a);
    i = 0;
    while (*a && *a != ' ' && i < 39)
        name[i++] = *a++;
    name[i] = 0;
    a = skip(a);
    type = (UINT)decnum(&a);
    h = lstrcmp(mod, "0") ? GetModuleHandle(mod) : 0;
    if (lstrcmp(mod, "0") && !h) {
        h = LoadLibrary(mod);
        if ((UINT)h < 32)
            h = 0;
    }
    r = MM_LOADCOMMANDRESOURCE(h, name[0] == '#' ? MAKEINTRESOURCE(lstrlen(name) - 1) : name, type);
    lg("l mciLoadCommandResource(%s, \"%s\", %u) = %u", (LPSTR)(h ? modname((WORD)h) : "0"),
       (LPSTR)name, type, r);
    flush();
}

static void freetable(const char *a)
{
    UINT n = (UINT)hexnum(&a);
    lg("f mciFreeCommandResource(%u) = %d", n, MM_FREECOMMANDRESOURCE(n));
    flush();
}

static void devid(const char *a)
{
    if (*a == '/') {
        DWORD el;
        a = skip(a + 1);
        el = hexnum(&a);
        a = skip(a);
        lg("i mciGetDeviceIDFromElementID(%08lX, \"%s\") = %u", el, (LPSTR)a,
           MM_GETDEVICEIDFROMELEMENTID(el, a));
    } else
        lg("i mciGetDeviceID(\"%s\") = %u", (LPSTR)a, mciGetDeviceID(a));
    flush();
}

void mci_section(void)
{
    static char ln[400], chunk[1024];
    HFILE f;
    int n = 0, at = 0, quiet = 0, k;
    char *p;
    HMODULE ml;
    UINT em = SetErrorMode(SEM_NOOPENFILEERRORBOX);
    ml = LoadLibrary("MMSYSLOG.DLL");
    SetErrorMode(em);
    if ((UINT)ml >= 32) {
        mldump = (MLDUMP)GetProcAddress(ml, "MLDUMP");
        mlname = (MLNAME)GetProcAddress(ml, "MLNAME");
        ((MLPROBE)GetProcAddress(ml, "MLPROBE"))(getss3(), GetCurrentTask());
        mlname((DWORD)(LPSTR)buf, "rbuf");
    }
    {
        /* 4 s first, the records and messages dropped: MMSYSTEM's start-up
           sound (2.5 s, played at boot) ends in that time, where under
           load it ended in the middle of a script (its WOM_DONE among a
           later step's records, or a play refused with 320 while it
           played) */
        DWORD t0 = GetTickCount();
        MSG m;
        while (GetTickCount() - t0 < 4000) {
            while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
            Yield();
        }
        if (mldump)
            mldump("NUL");
    }
    f = _lopen("C:\\MCI.TXT", OF_READ);
    if (f == HFILE_ERROR) {
        lg("no C:\\MCI.TXT");
        return;
    }
    for (;;) {
        MLDUMP save = mldump;
        int eof = 0;
        /* the next line, read a chunk at a time */
        for (k = 0;;) {
            if (at == n) {
                n = _lread(f, chunk, sizeof chunk);
                at = 0;
                if (n <= 0) {
                    n = 0;
                    eof = 1;
                    break;
                }
            }
            if (chunk[at] == '\r' || chunk[at] == '\n') {
                at++;
                if (k)
                    break;
                continue;
            }
            if (k < (int)sizeof ln - 1)
                ln[k++] = chunk[at];
            at++;
        }
        ln[k] = 0;
        if (eof && !k)
            break;
        p = ln;
        if (!*p || *p == '#')
            continue;
        if (quiet) {
            quiet--;
            mldump = 0;
        }
        switch (*p) {
        case ':': lg("== %s", (LPSTR)skip(p + 1)); break;
        case 's': sendstring(p + 1); break;
        case 'e': errstrings(skip(p + 1)); break;
        case 'E': errsize(skip(p + 1)); break;
        case 'i': devid(skip(p + 1)); break;
        case 'c': sendcommand(skip(p + 1)); break;
        case 'x': execute(skip(p + 1)); break;
        case 't': mctset(skip(p + 1), 0); break;
        case 'T': mctset(skip(p + 1), 1); break;
        case 'w': {
            const char *a = skip(p + 1);
            waitms((DWORD)decnum(&a));
            break;
        }
        case 'y': yieldtests(skip(p + 1)); break;
        case 'd': helpers(skip(p + 1)); break;
        case 'l': loadtable(skip(p + 1)); break;
        case 'f': freetable(skip(p + 1)); break;
        case 'p': {
            const char *a = skip(p + 1);
            quiet = (int)decnum(&a);
            break;
        }
        default: lg("? %s", (LPSTR)p); break;
        }
        if (save && !mldump) {
            mldump = save;
            mldump("NUL");
        }
    }
    _lclose(f);
}
