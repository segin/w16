/*
 * tk68.c - the command line KERNEL gives WINOLDAP for long WinExec command
 * lines. Run as the shell (tests/dropin.sh -i 'boot:shell=C:\TK68.EXE'),
 * with TK68W.EXE (tests/apps/tk68w.c) copied over SYSTEM\WINOLDAP.MOD and
 * SYSTEM\WINOA386.MOD and to C:\TK68W.EXE. For a .COM, a .BAT and a .PIF
 * (made here) and for TK68W.EXE itself as a Windows program, WinExec runs
 * the program with arguments of several lengths; each call's arguments'
 * length and result go to C:\TK68.LOG ("C ..."), followed by what TK68W
 * saw ("W ..."). The arguments are "123456789A123456789B...": the
 * character at position 10k is a letter, so a cut shows where it fell.
 * C:\TK68.TXT, if there, names the calls to make instead: c (.COM), b
 * (.BAT), p (.PIF), w (TK68W.EXE), d (C:\WC1.COM, for the real WINOLDAP
 * with TK68 run from WIN.INI run=) followed by the arguments' length in
 * decimal, e.g. "c117 b140 w200" (boot-test case kcmdline).
 */
#include <windows.h>

static char line[200];
static int ln;
static char cmd[400];

static void put(const char *s)
{
    while (*s)
        line[ln++] = *s++;
}

static void hex(WORD v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--)
        line[ln++] = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
}

static void flush(void)
{
    HFILE f = _lopen("C:\\TK68.LOG", OF_READWRITE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\TK68.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    line[ln++] = '\r';
    line[ln++] = '\n';
    _lwrite(f, line, ln);
    _lclose(f);
    ln = 0;
}

static void make(const char *name, const char *text, WORD n)
{
    HFILE f = _lcreat(name, 0);
    if (f != HFILE_ERROR) {
        _lwrite(f, text, n);
        _lclose(f);
    }
}

static void wait_gone(void)
{
    int i;
    MSG m;
    for (i = 0; i < 30000 && (GetModuleHandle("TK68W") || GetModuleHandle("WINOLDAP")); i++) {
        PeekMessage(&m, 0, 0, 0, PM_REMOVE);
        Yield();
    }
}

static void run(const char *prog, WORD n, int sp)
{
    WORD i = 0, k, r;
    while (prog[i]) {
        cmd[i] = prog[i];
        i++;
    }
    if (n || sp)
        cmd[i++] = ' ';
    if (sp)
        cmd[i++] = ' ';
    for (k = 1; k <= n; k++)
        cmd[i++] = (char)(k % 10 ? '0' + k % 10 : 'A' + (k / 10 - 1) % 26);
    cmd[i] = 0;
    put("C ");
    put(prog);
    put(" args=");
    hex(n, 4);
    put(" cmd=");
    hex(i, 4);
    flush();
    r = WinExec(cmd, SW_SHOWNORMAL);
    put("C result=");
    hex(r < 32 ? r : 0xFFFF, 4);
    flush();
    wait_gone();
}

static const WORD dos_n[] = { 0, 1, 89, 114, 115, 116, 117, 118, 125, 126, 127,
                              128, 129, 140, 200, 240, 243, 244, 245, 246, 250,
                              260, 300, 0xFFFF };
static const WORD bat_n[] = { 0, 115, 116, 117, 118, 140, 244, 245, 300, 0xFFFF };
static const WORD win_n[] = { 0, 100, 125, 126, 127, 128, 140, 200, 260, 0xFFFF };

int __cdecl tmain(void)
{
    static char code[512];
    int i;
    static char list[200];
    char *tail = list;
    HFILE f = _lopen("C:\\TK68.TXT", OF_READ);

    if (f != HFILE_ERROR) {
        int k = _lread(f, list, sizeof list - 1);
        list[k > 0 ? k : 0] = 0;
        _lclose(f);
        for (k = 0; list[k]; k++)
            if (list[k] == '\r' || list[k] == '\n')
                list[k] = ' ';
    }

    for (i = 0; i < (int)sizeof code; i++)
        code[i] = (char)0xC3;
    make("C:\\T68.COM", code, sizeof code);
    make("C:\\T68.BAT", "@echo off\r\n", 11);
    make("C:\\T68.PIF", code, sizeof code);
    put("C start mode=");
    put(GetWinFlags() & WF_ENHANCED ? "E" : "S");
    flush();
    while (*tail == ' ')
        tail++;
    if (*tail) {
        /* only the calls named: c (.COM), b (.BAT), p (.PIF), w (TK68W)
           followed by the arguments' length in decimal, e.g. "c244 w128" */
        while (*tail) {
            char k = *tail++;
            WORD n = 0;
            while (*tail >= '0' && *tail <= '9')
                n = n * 10 + (*tail++ - '0');
            run(k == 'b' ? "C:\\T68.BAT" : k == 'p' ? "C:\\T68.PIF" :
                k == 'w' ? "C:\\TK68W.EXE" : k == 'd' ? "C:\\WC1.COM" : "C:\\T68.COM", n, k == 's');
            while (*tail == ' ')
                tail++;
        }
        put("C done");
        flush();
        return 0;
    }
    for (i = 0; dos_n[i] != 0xFFFF; i++)
        run("C:\\T68.COM", dos_n[i], 0);
    for (i = 0; bat_n[i] != 0xFFFF; i++)
        run("C:\\T68.BAT", bat_n[i], 0);
    for (i = 0; bat_n[i] != 0xFFFF; i++)
        run("C:\\T68.PIF", bat_n[i], 0);
    for (i = 0; win_n[i] != 0xFFFF; i++)
        run("C:\\TK68W.EXE", win_n[i], 0);
    put("C done");
    flush();
    return 0;
}
