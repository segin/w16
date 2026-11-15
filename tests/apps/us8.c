/*
 * us8.c - probe US8: USER's network functions (WNet*, USER.499-532) with
 * no network driver, as File Manager calls them to find network drives
 * (WNetGetConnection on each drive, WNetGetCaps). Run as the shell on
 * Windows 3.1 (tests/refrun.sh) and on w16; compare TK.LOG.
 */
#include <windows.h>

#define TK_REF "us8ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern WORD FAR PASCAL WNetGetCaps(WORD);
extern WORD FAR PASCAL WNetGetUser(LPSTR, WORD FAR *);
extern WORD FAR PASCAL WNetGetError(WORD FAR *);
extern WORD FAR PASCAL WNetGetDirectoryType(LPSTR, WORD FAR *);
extern WORD FAR PASCAL WNetRestoreConnection(HWND, LPSTR);
extern WORD FAR PASCAL WNetGetErrorText(WORD, LPSTR, WORD FAR *);

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    char buf[64], name[8];
    WORD n, i, r;
    WaitEvent(0);
    InitApp(hi);
    out("US8 start\r\n");
    for (i = 0; i <= 12; i++) {
        lstrcpy(name, "caps 0");
        name[5] = (char)(i < 10 ? '0' + i : 'A' + i - 10);
        obs(name, WNetGetCaps(i));
    }
    obs("caps FFFF", WNetGetCaps(0xFFFF));
    lstrcpy(name, "A:");
    for (i = 0; i < 4; i++) {
        name[0] = (char)("ACDZ"[i]);
        buf[0] = 'q';
        buf[1] = 0;
        n = sizeof buf;
        r = WNetGetConnection(name, buf, (UINT FAR *)&n);
        obs(name, r);
        obs("  len", n);
        obs("  buf0", (BYTE)buf[0]);
    }
    n = sizeof buf;
    buf[0] = 'q';
    obs("getuser", WNetGetUser(buf, &n));
    obs("  len", n);
    obs("  buf0", (BYTE)buf[0]);
    n = 0x5555;
    obs("geterror", WNetGetError(&n));
    obs("  err", n);
    n = 0x5555;
    obs("dirtype", WNetGetDirectoryType("C:\\", &n));
    obs("  type", n);
    obs("addconn", WNetAddConnection("\\\\X\\Y", "", "E:"));
    obs("cancelconn", WNetCancelConnection("E:", FALSE));
    obs("restoreconn", WNetRestoreConnection(NULL, "E:"));
    n = sizeof buf;
    buf[0] = 'q';
    obs("errortext", WNetGetErrorText(1, buf, &n));
    obs("  len", n);
    obs("  buf0", (BYTE)buf[0]);
    out("US8 end\r\n");
    return 0;
}
