/*
 * sh9.c - SH9: RegisterShellHook (SHELL.102) and ShellHookProc (SHELL.103):
 * what the registered window receives when top-level windows are created,
 * destroyed and activated, for each value of the second argument.
 * Every message the window gets above WM_USER (and registered ones, by
 * name) is logged with its arguments, and as counts that obsref checks.
 */
#include <windows.h>

#define TK_REF "sh9ref.h"
#include "tklog.h"
#include "shprobe.h"

static HWND mainw, created;
static HINSTANCE inst;
static WORD nmsg;
static int logging;

typedef LRESULT (FAR PASCAL *HOOKPROC3)(int, WPARAM, LPARAM);

static void logmsg(UINT m, WPARAM wp, LPARAM lp)
{
    char name[64];
    out("MSG ");
    outhex(m);
    if (m >= 0xC000 && GetClipboardFormatName(m, name, sizeof name)) {
        out(" \"");
        out(name);
        out("\"");
    }
    out(" wp=");
    if (wp && (HWND)wp == created)
        out("created");
    else if (wp && (HWND)wp == mainw)
        out("main");
    else
        outhex(wp);
    out(" lp=");
    outhex(HIWORD(lp));
    outhex(LOWORD(lp));
    out(InSendMessage() ? " sent" : " posted");
    out("\r\n");
    nmsg++;
}

LRESULT CALLBACK __export MainProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (logging && (m >= WM_USER || m == WM_ACTIVATEAPP))
        logmsg(m, wp, lp);
    return DefWindowProc(w, m, wp, lp);
}

static HWND top(const char *title)
{
    HWND w = CreateWindow("SH9", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 200, 120,
                          NULL, NULL, inst, NULL);
    created = w;
    return w;
}

static void cycle(const char *tag)
{
    HWND w, c;
    out("-- ");
    out(tag);
    out("\r\n");
    nmsg = 0;
    w = top("created");
    pump(300);
    out("child\r\n");
    c = CreateWindow("SH9", "child", WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, w, NULL, inst, NULL);
    pump(200);
    DestroyWindow(c);
    out("activate main\r\n");
    SetActiveWindow(mainw);
    pump(200);
    out("activate created\r\n");
    SetActiveWindow(w);
    pump(200);
    out("hide\r\n");
    ShowWindow(w, SW_HIDE);
    pump(200);
    out("show\r\n");
    ShowWindow(w, SW_SHOW);
    pump(200);
    out("destroy\r\n");
    DestroyWindow(w);
    pump(300);
    obs(tag, nmsg);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HINSTANCE sh;
    HOOKPROC3 hp;
    WORD k;
    char tag[16];

    inst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(inst);
    out("SH9 shell hook\r\n");
    wc.style = 0;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.lpszClassName = "SH9";
    RegisterClass(&wc);
    mainw = CreateWindow("SH9", "SH9 main", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 200,
                         NULL, NULL, inst, NULL);
    pump(300);
    logging = 1;
    cycle("none");
    for (k = 0; k < 6; k++) {
        lstrcpy(tag, "reg0");
        tag[3] = (char)('0' + k);
        obs(tag, (WORD)RegisterShellHook(mainw, k));
        lstrcpy(tag, "cycle0");
        tag[5] = (char)('0' + k);
        cycle(tag);
    }
    obs("reg.null", (WORD)RegisterShellHook(NULL, 1));
    obs("reg.bad", (WORD)RegisterShellHook((HWND)0x1234, 1));
    obs("reg.off", (WORD)RegisterShellHook(mainw, 0));
    cycle("after.off");

    /* the hook procedure itself, called directly */
    sh = GetModuleHandle("SHELL");
    hp = (HOOKPROC3)GetProcAddress(sh, MAKEINTRESOURCE(103));
    obs("hookproc", hp != NULL);
    RegisterShellHook(mainw, 1);
    for (k = 0; k < 5; k++) {
        LRESULT r;
        lstrcpy(tag, "hp0");
        tag[2] = (char)('0' + k);
        out("direct ");
        out(tag);
        out("\r\n");
        nmsg = 0;
        r = hp(k, (WPARAM)mainw, 0x12345678L);
        pump(200);
        obs(tag, nmsg);
        tag[0] = 'r';
        obs_l(tag, r);
    }
    /* HSHELL_ACTIVATESHELLWINDOW with another window active */
    {
        HWND o = top("other");
        pump(300);
        obs("act.before", GetActiveWindow() == o);
        nmsg = 0;
        hp(3, 0, 0);
        pump(300);
        obs("act.msgs", nmsg);
        obs("act.main", GetActiveWindow() == mainw);
        obs("act.other", GetActiveWindow() == o);
        ShowWindow(mainw, SW_MINIMIZE);
        SetActiveWindow(o);
        pump(300);
        hp(3, 0, 0);
        pump(300);
        obs("act.iconic", IsIconic(mainw));
        obs("act.main2", GetActiveWindow() == mainw);
        DestroyWindow(o);
    }
    RegisterShellHook(mainw, 0);
    logging = 0;
    /* every registered message name */
    {
        UINT m;
        char name[64];
        for (m = 0xC000; m != 0; m++)
            if (GetClipboardFormatName(m, name, sizeof name)) {
                out("REGMSG ");
                outhex(m);
                out(" ");
                out(name);
                out("\r\n");
            }
    }
    out("SH9 done\r\n");
    return failures;
}
