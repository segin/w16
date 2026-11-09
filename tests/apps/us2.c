/*
 * us2.c - probe US2: USER's values and pure functions (ARCHITECTURE.md 10).
 *
 * Observes, on the system it runs on, what w16's USER must give exactly:
 * GetSystemMetrics, GetSysColor, SystemParametersInfo, the dialog base
 * units and other settings; the system window classes (GetClassInfo);
 * the desktop window; the character functions (AnsiUpper/AnsiLower,
 * IsCharAlpha/Upper/Lower/AlphaNumeric over 1..255), the sort order of
 * lstrcmp and lstrcmpi over single-character strings, and wsprintf.
 * Run it as the shell: tests/refrun.sh build/dos/US2.EXE (Windows 3.1)
 * or with tests/userboot.sh (w16's USER). Values go to TK.LOG as OBS lines
 * (tklog.h); us2ref.h holds Windows 3.1's (tools/obsref.py).
 */
#include <windows.h>

#define TK_REF "us2ref.h"
#include "tklog.h"

extern WORD __cdecl init_regs[7];

static char buf[300];

static void obs_hexstr(const char *name, const BYTE FAR *p, WORD n)
{
    WORD i, k = 0;
    for (i = 0; i < n && k < sizeof buf - 3; i++) {
        buf[k++] = "0123456789ABCDEF"[p[i] >> 4];
        buf[k++] = "0123456789ABCDEF"[p[i] & 15];
    }
    buf[k] = 0;
    obs_str(name, buf);
}

static const char *classes[] = {
    "BUTTON", "EDIT", "STATIC", "LISTBOX", "SCROLLBAR", "COMBOBOX", "COMBOLBOX",
    "MDICLIENT", "#32768", "#32769", "#32770", "#32771", "#32772", 0
};

/* sort the 255 one-character strings with cmp; print the order, '=' between
   equal neighbours */
static void sort_order(const char *name, int (FAR PASCAL *cmp)(LPCSTR, LPCSTR))
{
    static BYTE ord[255];
    static char s1[2], s2[2];
    WORD i, j, k, line = 0;
    char nm[32];
    for (i = 0; i < 255; i++)
        ord[i] = (BYTE)(i + 1);
    for (i = 1; i < 255; i++) {            /* insertion sort */
        BYTE c = ord[i];
        s1[0] = c;
        for (j = i; j > 0; j--) {
            s2[0] = ord[j - 1];
            if (cmp(s2, s1) <= 0)
                break;
            ord[j] = ord[j - 1];
        }
        ord[j] = c;
    }
    /* lines of 32 entries: hex byte, followed by '=' if equal to the next */
    for (i = 0; i < 255; i += 32, line++) {
        k = 0;
        for (j = i; j < i + 32 && j < 255; j++) {
            buf[k++] = "0123456789ABCDEF"[ord[j] >> 4];
            buf[k++] = "0123456789ABCDEF"[ord[j] & 15];
            if (j + 1 < 255) {
                s1[0] = ord[j];
                s2[0] = ord[j + 1];
                buf[k++] = cmp(s1, s2) == 0 ? '=' : ' ';
            }
        }
        buf[k] = 0;
        wsprintf(nm, "%s.%d", (LPSTR)name, line);
        obs_str(nm, buf);
    }
}

int __cdecl tmain(void)
{
    WORD i;
    char nm[48];
    out("US2 start\r\n");
    for (i = 0; i <= 48; i++)
        obsn("GetSystemMetrics", i, GetSystemMetrics(i));
    for (i = 0; i <= 24; i++) {
        DWORD c = GetSysColor(i);
        obsn("GetSysColor.lo", i, LOWORD(c));
        obsn("GetSysColor.hi", i, HIWORD(c));
    }
    {
        DWORD d = GetDialogBaseUnits();
        obs("GetDialogBaseUnits.x", LOWORD(d));
        obs("GetDialogBaseUnits.y", HIWORD(d));
    }
    obs("GetDoubleClickTime", GetDoubleClickTime());
    obs("GetCaretBlinkTime", GetCaretBlinkTime());
    obs("GetTimerResolution.lo", LOWORD(GetTimerResolution()));
    obs("GetTimerResolution.hi", HIWORD(GetTimerResolution()));
    obs("GetKeyboardType(0)", GetKeyboardType(0));
    obs("GetKeyboardType(1)", GetKeyboardType(1));
    obs("GetKeyboardType(2)", GetKeyboardType(2));
    obs("GetVersion.lo", LOWORD(GetVersion()));
    /* SystemParametersInfo: every SPI_GET* of the 3.1 SDK */
    {
        static const WORD spi[] = { 1, 3, 5, 10, 14, 16, 22, 24, 25, 27, 31, 0 };
        WORD v[16];
        for (i = 0; spi[i]; i++) {
            WORD j, r;
            for (j = 0; j < 16; j++)
                v[j] = 0xEEEE;
            r = SystemParametersInfo(spi[i], 0, v, 0);
            obsn("SPI.ret", spi[i], r);
            if (spi[i] == 31) {         /* SPI_GETICONTITLELOGFONT: a LOGFONT */
                obs_hexstr("SPI.ICONTITLELOGFONT", (BYTE FAR *)v, 18);
                obs_str("SPI.ICONTITLELOGFONT.face", (LPSTR)v + 18);
            } else {
                obsn("SPI.v0", spi[i], v[0]);
                obsn("SPI.v1", spi[i], v[1]);
                obsn("SPI.v2", spi[i], v[2]);
            }
        }
    }
    /* the desktop window */
    {
        HWND d = GetDesktopWindow();
        RECT r;
        GetWindowRect(d, &r);
        obs("desktop.left", r.left);
        obs("desktop.top", r.top);
        obs("desktop.right", r.right);
        obs("desktop.bottom", r.bottom);
        GetClientRect(d, &r);
        obs("desktop.client.right", r.right);
        obs("desktop.client.bottom", r.bottom);
        obs("desktop.style.lo", LOWORD(GetWindowLong(d, GWL_STYLE)));
        obs("desktop.style.hi", HIWORD(GetWindowLong(d, GWL_STYLE)));
        obs("desktop.exstyle.lo", LOWORD(GetWindowLong(d, GWL_EXSTYLE)));
        obs("desktop.parent", GetParent(d));
        obs("desktop.IsWindowVisible", IsWindowVisible(d));
        GetClassName(d, buf, sizeof buf);
        obs_str("desktop.class", buf);
        obs("desktop.GetWindowTextLength", GetWindowTextLength(d));
        obs_env("desktop.hwnd", d);
        obs("desktop.hwnd&3", (WORD)d & 3);
    }
    /* system classes */
    for (i = 0; classes[i]; i++) {
        WNDCLASS wc;
        BOOL r;
        { WORD k; for (k = 0; k < sizeof wc; k++) ((BYTE *)&wc)[k] = 0xEE; }
        r = GetClassInfo(0, classes[i], &wc);
        wsprintf(nm, "class.%s.found", (LPSTR)classes[i]);
        obs(nm, r);
        if (!r)
            continue;
        wsprintf(nm, "class.%s.style", (LPSTR)classes[i]);
        obs(nm, wc.style);
        wsprintf(nm, "class.%s.cbClsExtra", (LPSTR)classes[i]);
        obs(nm, wc.cbClsExtra);
        wsprintf(nm, "class.%s.cbWndExtra", (LPSTR)classes[i]);
        obs(nm, wc.cbWndExtra);
        wsprintf(nm, "class.%s.hbrBackground", (LPSTR)classes[i]);
        obs((WORD)wc.hbrBackground <= 40 ? nm : (wsprintf(nm, "~class.%s.hbrBackground", (LPSTR)classes[i]), nm),
            wc.hbrBackground);
        wsprintf(nm, "class.%s.hCursor!=0", (LPSTR)classes[i]);
        obs(nm, wc.hCursor != 0);
        wsprintf(nm, "class.%s.hIcon!=0", (LPSTR)classes[i]);
        obs(nm, wc.hIcon != 0);
        wsprintf(nm, "class.%s.menu", (LPSTR)classes[i]);
        obs(nm, wc.lpszMenuName != 0);
        wsprintf(nm, "class.%s.wndproc", (LPSTR)classes[i]);
        obs_env(nm, HIWORD((DWORD)wc.lpfnWndProc));
    }
    /* character functions */
    {
        static BYTE up[256], lo[256], fl[256];
        for (i = 0; i < 256; i++) {
            up[i] = (BYTE)(DWORD)AnsiUpper((LPSTR)MAKELP(0, i));
            lo[i] = (BYTE)(DWORD)AnsiLower((LPSTR)MAKELP(0, i));
            fl[i] = (IsCharAlpha((char)i) ? 1 : 0) | (IsCharUpper((char)i) ? 2 : 0) |
                    (IsCharLower((char)i) ? 4 : 0) | (IsCharAlphaNumeric((char)i) ? 8 : 0);
        }
        for (i = 0; i < 256; i += 64) {
            wsprintf(nm, "AnsiUpper.%02X", i);
            obs_hexstr(nm, up + i, 64);
            wsprintf(nm, "AnsiLower.%02X", i);
            obs_hexstr(nm, lo + i, 64);
            wsprintf(nm, "IsChar.%02X", i);
            obs_hexstr(nm, fl + i, 64);
        }
    }
    sort_order("lstrcmp", lstrcmp);
    sort_order("lstrcmpi", lstrcmpi);
    {
        static const char *pairs[][2] = {
            { "a", "ab" }, { "ab", "a" }, { "", "a" }, { "a-b", "ab" }, { "ab", "a-b" },
            { "co-op", "coop" }, { "A", "a" }, { "a", "B" }, { "Zz", "zZ" }, { "\xe9", "e" },
            { "e", "\xe9" }, { "\xc9", "\xe9" }, { "x'y", "xy" }, { "1", "a" }, { "_", "a" }, { 0, 0 }
        };
        for (i = 0; pairs[i][0]; i++) {
            wsprintf(nm, "lstrcmp.pair%d", i);
            obs(nm, (WORD)lstrcmp(pairs[i][0], pairs[i][1]));
            wsprintf(nm, "lstrcmpi.pair%d", i);
            obs(nm, (WORD)lstrcmpi(pairs[i][0], pairs[i][1]));
        }
    }
    /* wsprintf */
    {
        static const char *fmts[] = {
            "%d|%5d|%-5d|%05d|%u|%x|%X|%04x|%c|%%", "%ld|%lu|%lx|%lX|%08lx", "%s|%10s|%-10s|%.2s|",
            "%i|%+d|% d|%#x", 0
        };
        int n;
        n = wsprintf(buf, fmts[0], -12, 34, 56, 78, 65535u, 0xBEEF, 0xBEEF, 0x1f, 'Q');
        obs("wsprintf.0.n", n);
        obs_str("wsprintf.0", buf);
        n = wsprintf(buf, fmts[1], -70000L, 4000000000UL, 0xDEADBEEFL, 0xDEADBEEFL, 0x12AL);
        obs("wsprintf.1.n", n);
        obs_str("wsprintf.1", buf);
        n = wsprintf(buf, fmts[2], (LPSTR)"abc", (LPSTR)"abc", (LPSTR)"abc", (LPSTR)"abc");
        obs("wsprintf.2.n", n);
        obs_str("wsprintf.2", buf);
        n = wsprintf(buf, fmts[3], 5, 5, 5, 255);
        obs("wsprintf.3.n", n);
        obs_str("wsprintf.3", buf);
    }
    obs("GetFreeSystemResources(0)>0", GetFreeSystemResources(0) > 0);
    out("US2 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
