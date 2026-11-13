/*
 * us11.c - probe US11: the argument sizes of USER exports whose parameter
 * lists are not in the SDK headers (user/ordinals.txt "?"), and what
 * Windows 3.1's USER returns for them; GetNextQueueWindow's choice.
 *
 * Each function is called through GetProcAddress(USER, ordinal) with more
 * words pushed than it can take; a Pascal function pops its own arguments,
 * so the stack pointer after the call tells how many bytes it took
 * ("pop"). The words pushed are chosen to be harmless as any type: the
 * selector of a zeroed 64 KB block, so that any two of them make a far
 * pointer into it (Schulman, Undocumented Windows ch. 6, gives the
 * parameter lists where it has them; the probe confirms them).
 * Run as the shell on Windows 3.1 (tests/refrun.sh); ordinals that end
 * Windows (OldExitWindows), take register arguments (StringFunc: CX) or
 * are not functions (_FFFE_FARFRAME, a frame helper) are left out.
 */
#include <windows.h>

#define TK_REF "us11ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static WORD args[16];
static DWORD lastret;

/* call f with n words of args (args[0] pushed first); bytes it popped */
static WORD callpop(FARPROC f, WORD n)
{
    WORD s0 = 0, s1 = 0, ra = 0, rd = 0;
    WORD __far *a = (WORD __far *)args;
    __asm {
        mov s0, sp
        les bx, a
        mov cx, n
    l1: jcxz l2
        push word ptr es:[bx]
        add bx, 2
        dec cx
        jmp l1
    l2: call dword ptr f
        mov ra, ax
        mov rd, dx
        mov s1, sp
        mov sp, s0
    }
    lastret = MAKELONG(ra, rd);
    return n * 2 - (s0 - s1);
}

static void probe(const char *name, WORD ord, WORD n, WORD first)
{
    FARPROC f = GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(ord));
    WORD pop;
    if (!f) {
        out(name);
        out(": no entry\r\n");
        return;
    }
    if (first != 0xFFFF)
        args[0] = first;
    pop = callpop(f, n);
    out("OBS ");
    out(name);
    out("-pop=");
    outhex(pop);
    out("\r\nOBS ~");
    out(name);
    out("-ret=");
    outhex(HIWORD(lastret));
    outhex(LOWORD(lastret));
    out("\r\n");
}

LRESULT CALLBACK __export WinProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    return DefWindowProc(w, m, wp, lp);
}

static HWND mk(HINSTANCE hi, const char *t, DWORD style, HWND owner)
{
    return CreateWindow("US11", t, style, 10, 10, 100, 60, owner, NULL, hi, NULL);
}

static char nm[8];
static const char *which(HWND h, HWND *ws, int n)
{
    int i;
    if (!h)
        return "0";
    for (i = 0; i < n; i++)
        if (ws[i] == h) {
            nm[0] = (char)('A' + i);
            nm[1] = 0;
            return nm;
        }
    return "other";
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    HGLOBAL hb;
    WORD sel, i;
    WNDCLASS wc;
    HWND ws[6];
    FARPROC gnq;
    MSG msg;
    WaitEvent(0);
    InitApp(hi);
    out("US11 start\r\n");
    hb = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 0xFFF0L);
    sel = SELECTOROF(GlobalLock(hb));
    for (i = 0; i < 16; i++)
        args[i] = sel;
    probe("Bear51", 51, 8, 'A');
    probe("QuerySendMessage", 184, 8, 0xFFFF);
    probe("LockInput", 226, 8, 0);
    args[0] = sel;
    probe("RepaintScreen", 275, 8, 0xFFFF);
    args[0] = 0;                        /* LockMyTask(FALSE) */
    probe("LockMyTask", 276, 8, 0);
    probe("WinOldAppHackOMatic", 322, 8, 0);
    args[0] = sel;
    PostMessage(NULL, WM_USER, 0, 0);
    /* GetMessage2(LPMSG, HWND, WORD, WORD, WORD, BOOL): the message just
       posted lets it return */
    for (i = 0; i < 16; i++)
        args[i] = 0;
    args[0] = sel;                      /* lpMsg: seg, off pushed in order */
    args[1] = 0;
    probe("GetMessage2", 323, 8, 0xFFFF);
    for (i = 0; i < 16; i++)
        args[i] = sel;
    probe("GetInternalIconHeader", 372, 8, 0xFFFF);
    {   /* again with two distinct far pointers into the block */
        BYTE __far *b = (BYTE __far *)MAKELP(sel, 0);
        args[0] = sel; args[1] = 0x100; args[2] = sel; args[3] = 0x200;
        for (i = 0; i < 64; i++) {
            b[0x100 + i] = (BYTE)(0x10 + i);
            b[0x200 + i] = 0xFF;
        }
        probe("GetInternalIconHeader2", 372, 4, 0xFFFF);
        out("OBS iih@100=");
        for (i = 0; i < 8; i++) { outhex(((WORD __far *)(b + 0x100))[i]); out(" "); }
        out("\r\nOBS iih@200=");
        for (i = 0; i < 24; i++) { outhex(((WORD __far *)(b + 0x200))[i]); out(" "); }
        out("\r\n");
        for (i = 0; i < 16; i++)
            args[i] = sel;
    }
    probe("GetUserLocalObjType", 480, 8, 0xFFFF);
    for (i = 0; i < 16; i++)
        args[i] = 0;                    /* hardware_event(0 = WM_NULL, ...) */
    probe("hardware_event", 481, 8, 0);
    probe("WNetEnable", 521, 8, 0);
    probe("WNetDisable", 522, 8, 0);
    {   /* GetFilePortName(LPOFSTRUCT): a real one (a pointer into the
           zeroed block faulted) */
        static OFSTRUCT of;
        OFSTRUCT __far *pof = &of;
        lstrcpy(of.szPathName, "C:\\WINDOWS\\WIN.INI");
        of.cBytes = sizeof of;
        for (i = 0; i < 16; i++)
            args[i] = 0;
        args[0] = SELECTOROF(pof);
        args[1] = OFFSETOF(pof);
        if (sel == 1) probe("GetFilePortName", 343, 8, 0xFFFF);
    }
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        DispatchMessage(&msg);

    /* GetNextQueueWindow(hwnd, nWhichWay): A..F top-level, made in that
       order (F ends on top): B hidden, C owned by A, D disabled */
    wc.style = 0;
    wc.lpfnWndProc = WinProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US11";
    RegisterClass(&wc);
    ws[0] = mk(hi, "A", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0);
    ws[1] = mk(hi, "B", WS_OVERLAPPEDWINDOW, 0);
    ws[2] = mk(hi, "C", WS_OVERLAPPEDWINDOW | WS_VISIBLE, ws[0]);
    ws[3] = mk(hi, "D", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_DISABLED, 0);
    ws[4] = mk(hi, "E", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0);
    ws[5] = mk(hi, "F", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0);
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        DispatchMessage(&msg);
    gnq = GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(274));
    out("Z:");
    {
        HWND h;
        for (h = GetWindow(ws[0], GW_HWNDFIRST); h; h = GetWindow(h, GW_HWNDNEXT)) {
            out(" ");
            out(which(h, ws, 6));
        }
    }
    out("\r\n");
    for (i = 0; i < 6; i++) {
        WORD pop;
        args[0] = (WORD)ws[i];
        args[1] = 0;
        pop = callpop(gnq, 2);
        out("OBS gnq-");
        out(which(ws[i], ws, 6));
        out("-0=");
        out(which((HWND)LOWORD(lastret), ws, 6));
        out("\r\n");
        args[1] = 1;
        callpop(gnq, 2);
        out("OBS gnq-");
        out(which(ws[i], ws, 6));
        out("-1=");
        out(which((HWND)LOWORD(lastret), ws, 6));
        out("\r\n");
        if (i == 0) {
            out("OBS gnq-pop=");
            outhex(pop);
            out("\r\n");
        }
    }
    for (i = 0; i < 6; i++)
        DestroyWindow(ws[5 - i]);
    out("US11 end\r\n");
    return 0;
}
