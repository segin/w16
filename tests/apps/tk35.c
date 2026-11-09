/*
 * tk35.c - where a task starts and what it knows of the Windows
 * directory: its current drive and directory (INT 21h 19h, 47h), the
 * windir and WINDIR strings of its environment (exact case), and
 * GetWindowsDirectory. Started by SNAP (itself started from WIN.INI's
 * load= line, PROGMAN being the shell), as Write was in the desktop
 * comparisons (tests/dropin.sh, and -r for Windows 3.1). Logs to
 * C:\TK35.LOG so that SNAP's TK.LOG does not mix in.
 */
#include <windows.h>

static WORD getdrv(void);
#pragma aux getdrv = "mov ah, 19h" "int 21h" "xor ah, ah" value [ax];
static void getdir(char FAR *p);
#pragma aux getdir = "push ds" "push es" "pop ds" "mov ah, 47h" "xor dl, dl" \
    "int 21h" "pop ds" parm [es si] modify [ax dx];

static void put(HFILE f, LPCSTR s)
{
    _lwrite(f, s, lstrlen(s));
}

int __cdecl tmain(void)
{
    HFILE f = _lcreat("C:\\TK35.LOG", 0);
    char buf[160];
    LPSTR env = GetDOSEnvironment();
    WORD drv = getdrv();
    buf[0] = (char)('A' + drv);
    buf[1] = ':';
    buf[2] = '\\';
    getdir(buf + 3);
    put(f, "OBS current directory=\"");
    put(f, buf);
    put(f, "\"\r\n");
    while (*env) {
        if ((env[0] == 'w' || env[0] == 'W') && (env[1] == 'i' || env[1] == 'I') &&
            (env[2] == 'n' || env[2] == 'N') && (env[3] == 'd' || env[3] == 'D') &&
            (env[4] == 'i' || env[4] == 'I') && (env[5] == 'r' || env[5] == 'R') &&
            env[6] == '=') {
            put(f, "OBS environment string=\"");
            put(f, env);
            put(f, "\"\r\n");
        }
        env += lstrlen(env) + 1;
    }
    GetWindowsDirectory(buf, sizeof buf);
    put(f, "OBS GetWindowsDirectory=\"");
    put(f, buf);
    put(f, "\"\r\n");
    _lclose(f);
    return 0;
}
