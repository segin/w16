/*
 * cp1.c - CP1.CPL, a Control Panel applet DLL that records how Control
 * Panel drives it (black-box probe for w16's CONTROL.EXE, tests/cpobs.sh).
 *
 * Every CPlApplet call is appended to C:\CP1.LOG: the message, its
 * parameters, the window it was given (class and caption) and what the
 * probe answered. What it answers comes from C:\CP1.INI, section [cp1]:
 *   count=N          applets (CPL_GETCOUNT), default 1
 *   init=0|1         CPL_INIT's answer, default 1
 *   inquire=0|1      answer CPL_INQUIRE (default 1)
 *   newinquire=0..4  CPL_NEWINQUIRE: 0 return 1, untouched (default); 1 fill
 *                    and return 0; 2 fill, return 1; 3 return 0, untouched;
 *                    4 fill with dwSize 0, return 0
 *   hiword=N         high word (DX) of the CPL_INIT and CPL_GETCOUNT answers
 *   namebase=N       CPL_INQUIRE names are string N+i (100: "Applet <i>",
 *                    300: X, WWW..., Probe Two, Ab, Abc...z), default 100
 *   name<i>=, info<i>=   CPL_NEWINQUIRE's name and description
 *   icon<i>=1..3     icon resource: 1 solid black square, 2 one-pixel
 *                    black frame, 3 white square (default 1)
 *   dblclk=box       on CPL_DBLCLK show a message box naming the applet
 * Two identical copies (CP1.CPL, CP2.CPL ...) can be told apart by
 * GetModuleFileName in the log.
 */
#include <windows.h>
#include "../../shell/cpl/cpl.h"

static HINSTANCE hinst;
static char ini[] = "C:\\CP1.INI";

static void logs(const char FAR *s)
{
    HFILE f;
    OFSTRUCT of;
    f = OpenFile("C:\\CP1.LOG", &of, OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\CP1.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    _lwrite(f, s, lstrlen(s));
    _lclose(f);
}

int __cdecl __far cpl_libmain(HINSTANCE h)
{
    hinst = h;
    logs("load\r\n");
    return 1;
}

static int geti(const char *key, int def)
{
    return GetPrivateProfileInt("cp1", key, def, ini);
}

LONG FAR PASCAL __loadds CPlApplet(HWND hwnd, UINT msg, LPARAM l1, LPARAM l2)
{
    char buf[300], cls[40], cap[60], key[16], mod[80];
    RECT rc;
    LONG r = 0;
    int i = (int)l1;

    cls[0] = cap[0] = 0;
    if (IsWindow(hwnd)) {
        GetClassName(hwnd, cls, sizeof cls);
        GetWindowText(hwnd, cap, sizeof cap);
        GetWindowRect(hwnd, &rc);
    } else
        SetRectEmpty(&rc);
    GetModuleFileName(hinst, mod, sizeof mod);
    switch (msg) {
    case CPL_INIT:
        r = MAKELONG(geti("init", 1), geti("hiword", 0));
        break;
    case CPL_GETCOUNT:
        r = MAKELONG(geti("count", 1), geti("hiword", 0));
        break;
    case CPL_INQUIRE:
        if (geti("inquire", 1)) {
            LPCPLINFO p = (LPCPLINFO)l2;
            wsprintf(key, "icon%d", i);
            p->idIcon = geti(key, 1);
            p->idName = geti("namebase", 100) + i;
            p->idInfo = 200 + i;
            p->lData = 0x1000 + i;
            r = 0;
        } else
            r = 1;
        break;
    case CPL_NEWINQUIRE: {
        int mode = geti("newinquire", 0);
        LPNEWCPLINFO p = (LPNEWCPLINFO)l2;
        wsprintf(buf, "  newinquire in: dwSize=%lu lData=%08lX hIcon=%04X name0=%d\r\n",
                 p->dwSize, p->lData, (UINT)p->hIcon, (int)(BYTE)p->szName[0]);
        logs(buf);
        if (mode == 1 || mode == 2 || mode == 4) {
            wsprintf(key, "icon%d", i);
            p->dwSize = mode == 4 ? 0 : sizeof *p;
            p->dwFlags = 0;
            p->dwHelpContext = 0;
            p->lData = 0x2000 + i;
            p->hIcon = LoadIcon(hinst, MAKEINTRESOURCE(geti(key, 1)));
            wsprintf(key, "name%d", i);
            GetPrivateProfileString("cp1", key, "N-CP1", p->szName, sizeof p->szName, ini);
            wsprintf(key, "info%d", i);
            GetPrivateProfileString("cp1", key, "new info", p->szInfo, sizeof p->szInfo, ini);
            p->szHelpFile[0] = 0;
        }
        r = mode == 0 || mode == 2;
        break;
    }
    case CPL_DBLCLK:
        GetPrivateProfileString("cp1", "dblclk", "", buf, 20, ini);
        if (!lstrcmp(buf, "box")) {
            wsprintf(buf, "applet %d", i);
            MessageBox(hwnd, buf, "CP1", MB_OK);
        }
        break;
    }
    wsprintf(buf, "%s msg=%u l1=%08lX l2=%08lX hwnd=%04X cls=\"%s\" cap=\"%s\" "
             "rect=%d,%d,%d,%d vis=%d en=%d -> %ld\r\n",
             (LPSTR)mod, msg, l1, l2, (UINT)hwnd, (LPSTR)cls, (LPSTR)cap,
             rc.left, rc.top, rc.right, rc.bottom, IsWindowVisible(hwnd),
             IsWindowEnabled(hwnd), r);
    logs(buf);
    return r;
}
