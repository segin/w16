/*
 * sh14.c - SH14: what starting a name does, for every kind of name, through
 * each of the four ways a shell has: FindExecutable, ShellExecute (SHELL),
 * WinExec and LoadModule (KERNEL). Names: a Windows program (with and
 * without its extension, with parameters), a .COM, a DOS .EXE, a 32-bit
 * Windows program (a PE image), a .BAT, a file without
 * an extension, registered documents (.TXT, .WRI), unregistered extensions
 * (.ABC, .XYZ), an extension WIN.INI maps to a missing program, a class
 * whose program is missing, missing files and directories, directories.
 *
 * Observed per call: the return code (an instance handle becomes 33) and
 * what actually ran: SH5.EXE's log (C:\SH5.TXT: its command tail), the
 * marker files the documents' contents write when DOS runs them as a .COM
 * program (C:\RANCOM.TXT; every document here is a tiny .COM program that
 * creates it and exits) and the batch file writes (C:\RANBAT.TXT), a
 * window of another program (closed again) and a dialog or message box
 * shown meanwhile (its caption and texts; cancelled). Run as the shell
 * with SH5.EXE in C:\ (tests/shellprobe.sh SH14, boot-test runassoc).
 * The error mode is SEM_NOOPENFILEERRORBOX, so that no KERNEL box stops
 * the run (KERNEL's own box: TK59).
 */
#include <windows.h>

#define TK_REF "sh14ref.h"
#include "tklog.h"
#include "shprobe.h"

#define HKCR ((HKEY)HKEY_CLASSES_ROOT)

static HWND mainw;
static HINSTANCE inst;
static char buf[400], seen[400];

static void mkdir(const char __far *p);
#pragma aux mkdir = "push ds" "mov ds, dx" "mov dx, ax" "mov ah, 39h" "int 21h" "pop ds" \
    parm [dx ax] modify [ax];

/* a .COM program: create C:\RANCOM.TXT, exit (the documents' contents) */
static const BYTE com[] = {
    0xB4, 0x3C,                 /* mov ah, 3Ch */
    0x31, 0xC9,                 /* xor cx, cx */
    0xBA, 0x13, 0x01,           /* mov dx, 113h (the name below) */
    0xCD, 0x21,                 /* int 21h */
    0x93,                       /* xchg bx, ax */
    0xB4, 0x3E,                 /* mov ah, 3Eh */
    0xCD, 0x21,                 /* int 21h */
    0xB8, 0x00, 0x4C,           /* mov ax, 4C00h */
    0xCD, 0x21,                 /* int 21h */
    'C', ':', '\\', 'R', 'A', 'N', 'C', 'O', 'M', '.', 'T', 'X', 'T', 0
};

/* a DOS .EXE program (MZ, no NE header) that does the same: a 64-byte
   header (no relocations, offset 3Ch 0), the code at 40h */
static const BYTE mz[] = {
    'M', 'Z', 99, 0, 1, 0, 0, 0,        /* last page 99 bytes, 1 page */
    4, 0, 0x20, 0, 0xFF, 0xFF, 4, 0,    /* header 4 paras; SS 4 */
    0, 1, 0, 0, 0, 0, 0, 0,             /* SP 100h; IP 0, CS 0 */
    0x40, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x0E, 0x1F,                 /* push cs; pop ds */
    0xB4, 0x3C, 0x31, 0xC9,
    0xBA, 0x15, 0x00,           /* mov dx, 15h (the name) */
    0xCD, 0x21, 0x93, 0xB4, 0x3E, 0xCD, 0x21, 0xB8, 0x00, 0x4C, 0xCD, 0x21,
    'C', ':', '\\', 'R', 'A', 'N', 'C', 'O', 'M', '.', 'T', 'X', 'T', 0
};

static void mkfile(const char *path, const void FAR *data, UINT n)
{
    HFILE f = _lcreat(path, 0);
    _lwrite(f, data, n);
    _lclose(f);
}

static void put16(BYTE *p, UINT o, WORD v)
{
    p[o] = (BYTE)v;
    p[o + 1] = (BYTE)(v >> 8);
}

static void put32(BYTE *p, UINT o, DWORD v)
{
    put16(p, o, LOWORD(v));
    put16(p, o + 2, HIWORD(v));
}

/* a 32-bit Windows program (a minimal PE32 image, the published PE/COFF
   layout; the same bytes as tests/scripts/runassocfiles.py's PE.EXE):
   mz[]'s header and code as its MS-DOS stub (2 pages, relocations at
   40h, e_lfanew 80h), "PE\0\0", the file header (i386, one section,
   optional header E0h bytes, flags 010Fh), the optional header (PE32,
   entry and code at 1000h, image base 400000h, alignment 1000h/200h,
   OS and subsystem 4.0, image 2000h, headers 200h, GUI, 1 MB/4 KB stack
   and heap, 16 empty directories), .text at 200h: RET. 400h bytes. */
static void mkpe(const char *path)
{
    static BYTE f[0x400];
    UINT o;
    for (o = 0; o < sizeof mz; o++)
        f[o] = mz[o];
    f[2] = 0;                           /* last page full, 2 pages */
    f[4] = 2;
    f[0x0A] = 0x10;                     /* minimum 10h paragraphs */
    f[0x0E] = 0x40;                     /* SS 40h: the stack past the image */
    f[0x3C] = 0x80;                     /* e_lfanew */
    o = 0x80;
    f[o] = 'P';
    f[o + 1] = 'E';
    put16(f, o + 4, 0x14C);
    put16(f, o + 6, 1);
    put16(f, o + 20, 0xE0);
    put16(f, o + 22, 0x010F);
    o += 24;
    put16(f, o, 0x10B);
    f[o + 2] = 2;
    f[o + 3] = 50;
    put32(f, o + 4, 0x200);
    put32(f, o + 16, 0x1000);
    put32(f, o + 20, 0x1000);
    put32(f, o + 24, 0x2000);
    put32(f, o + 28, 0x400000L);
    put32(f, o + 32, 0x1000);
    put32(f, o + 36, 0x200);
    put16(f, o + 40, 4);
    put16(f, o + 48, 4);
    put32(f, o + 56, 0x2000);
    put32(f, o + 60, 0x200);
    put16(f, o + 68, 2);
    put32(f, o + 72, 0x100000L);
    put32(f, o + 76, 0x1000);
    put32(f, o + 80, 0x100000L);
    put32(f, o + 84, 0x1000);
    put32(f, o + 92, 16);
    o += 0xE0;
    lstrcpy((LPSTR)(f + o), ".text");
    put32(f, o + 8, 1);
    put32(f, o + 12, 0x1000);
    put32(f, o + 16, 0x200);
    put32(f, o + 20, 0x200);
    put32(f, o + 36, 0x60000020L);
    f[0x200] = 0xC3;
    mkfile(path, f, sizeof f);
}

static void copyfile(const char *from, const char *to)
{
    static char b[512];
    HFILE in = _lopen(from, OF_READ), o;
    UINT n;
    if (in == HFILE_ERROR) {
        out("copy: no ");
        out(from);
        out("\r\n");
        return;
    }
    o = _lcreat(to, 0);
    while ((n = _lread(in, b, sizeof b)) != 0 && n != (UINT)-1)
        _lwrite(o, b, n);
    _lclose(o);
    _lclose(in);
}

static void add_seen(const char FAR *s)
{
    if (lstrlen(seen) + lstrlen(s) < sizeof seen - 2)
        lstrcat(seen, s);
}

/* a dialog's caption and its controls' texts */
static void note_dialog(HWND d)
{
    HWND c;
    char t[200];
    GetWindowText(d, t, sizeof t);
    add_seen("[");
    add_seen(t);
    for (c = GetWindow(d, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        GetWindowText(c, t, sizeof t);
        if (t[0]) {
            add_seen("|");
            add_seen(t);
        }
    }
    add_seen("]");
}

/* every half second: a dialog that is up (ours or another program's) is
   noted and cancelled; another program's window is noted (its module)
   and closed */
void CALLBACK __export Watch(HWND w, UINT m, UINT id, DWORD t)
{
    HWND x;
    char cls[40], mod[80];
    (void)w; (void)m; (void)id; (void)t;
    for (x = GetWindow(GetDesktopWindow(), GW_CHILD); x; x = GetWindow(x, GW_HWNDNEXT)) {
        HINSTANCE hi;
        int n, i;
        if (x == mainw || !IsWindowVisible(x))
            continue;
        GetClassName(x, cls, sizeof cls);
        if (!lstrcmp(cls, "#32770")) {
            if (GetProp(x, "SH14"))
                continue;
            SetProp(x, "SH14", (HANDLE)1);
            note_dialog(x);
            PostMessage(x, WM_COMMAND, IDCANCEL, 0);
            return;
        }
        hi = (HINSTANCE)GetWindowWord(x, GWW_HINSTANCE);
        if (hi == inst)
            continue;
        n = GetModuleFileName(hi, mod, sizeof mod);
        for (i = n; i > 0 && mod[i - 1] != '\\'; i--)
            ;
        /* WINOLDAP's own windows (a DOS program running) are left alone */
        if (!lstrcmpi(mod + i, "WINOLDAP.MOD") || !lstrcmpi(mod + i, "WINOA386.MOD"))
            continue;
        if (GetProp(x, "SH14"))
            continue;
        SetProp(x, "SH14", (HANDLE)1);
        add_seen("<");
        add_seen(mod + i);
        add_seen(">");
        PostMessage(x, WM_CLOSE, 0, 0);
    }
}

/* the contents of a marker file (then deleted), "" without one */
static void marker(const char *name, const char *file, const char *suf)
{
    char n[64];
    OFSTRUCT of;
    HFILE f = _lopen(file, OF_READ);
    UINT k = 0;
    buf[0] = 0;
    if (f != HFILE_ERROR) {
        k = _lread(f, buf, sizeof buf - 1);
        _lclose(f);
        if (k == (UINT)-1)
            k = 0;
        buf[k] = 0;
        while (k && (buf[k - 1] == '\r' || buf[k - 1] == '\n' || buf[k - 1] == ' '))
            buf[--k] = 0;
        if (!k)
            lstrcpy(buf, "yes");
        OpenFile(file, &of, OF_DELETE);
    }
    suffix(n, name, suf);
    obs_s(n, buf);
}

/* after a call: wait for what it started, then note what ran */
static void after(const char *name, UINT r)
{
    char n[64];
    int i;
    obs(name, r > 32 ? 33 : r);
    pump(1500);
    /* a DOS program: until WINOLDAP has gone (20 s at most) */
    for (i = 0; i < 40 && GetModuleHandle("WINOLDAP"); i++)
        pump(500);
    pump(500);
    marker(name, "C:\\SH5.TXT", ".sh5");
    marker(name, "C:\\RANCOM.TXT", ".com");
    marker(name, "C:\\RANBAT.TXT", ".bat");
    suffix(n, name, ".seen");
    obs_s(n, seen);
    seen[0] = 0;
    /* the programs closed meanwhile */
    pump(500);
}

static void fe(const char *name, LPCSTR file)
{
    char n[64];
    UINT r;
    lstrcpy(buf, "XXXX");
    r = (UINT)FindExecutable(file, NULL, buf);
    obs(name, r > 32 ? 33 : r);
    suffix(n, name, ".res");
    obs_s(n, r > 32 ? buf : "");
    seen[0] = 0;
}

static void se(const char *name, LPCSTR file, LPCSTR params)
{
    after(name, (UINT)ShellExecute(mainw, NULL, file, params, NULL, SW_SHOWNORMAL));
}

static void we(const char *name, LPCSTR file, LPCSTR params)
{
    static char cmd[160];
    lstrcpy(cmd, file);
    if (params) {
        lstrcat(cmd, " ");
        lstrcat(cmd, params);
    }
    after(name, WinExec(cmd, SW_SHOWNORMAL));
}

static void lm(const char *name, LPCSTR file, LPCSTR params)
{
    static BYTE tail[130];
    static WORD show[2] = { 2, SW_SHOWNORMAL };
    struct { WORD env; LPBYTE cmd; LPVOID show; DWORD res; } lp;
    UINT n = 0, i;
    if (params) {                       /* " params", as DOS has a tail */
        tail[1 + n++] = ' ';
        for (i = 0; params[i] && n < sizeof tail - 3; i++)
            tail[1 + n++] = params[i];
    }
    tail[0] = (BYTE)n;
    tail[1 + n] = '\r';
    lp.env = 0;
    lp.cmd = tail;
    lp.show = show;
    lp.res = 0;
    after(name, (UINT)LoadModule(file, &lp));
}

/* all four for one name */
static void all(const char *kind, LPCSTR file, LPCSTR params)
{
    char n[48];
    suffix(n, "fe.", kind);
    fe(n, file);
    suffix(n, "se.", kind);
    se(n, file, params);
    suffix(n, "we.", kind);
    we(n, file, params);
    suffix(n, "lm.", kind);
    lm(n, file, params);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    OFSTRUCT of;

    inst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(inst);
    SetErrorMode(SEM_NOOPENFILEERRORBOX);
    out("SH14 names: FindExecutable, ShellExecute, WinExec, LoadModule\r\n");
    wc.style = 0;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = inst;
    wc.lpszClassName = "SH14";
    RegisterClass(&wc);
    mainw = CreateWindow("SH14", "SH14", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 100,
                         NULL, NULL, inst, NULL);
    SetTimer(mainw, 1, 500, (TIMERPROC)MakeProcInstance((FARPROC)Watch, inst));

    OpenFile("C:\\SH5.TXT", &of, OF_DELETE);
    OpenFile("C:\\RANCOM.TXT", &of, OF_DELETE);
    OpenFile("C:\\RANBAT.TXT", &of, OF_DELETE);
    mkdir("C:\\RA");
    mkdir("C:\\RA\\D.ABC");
    copyfile("C:\\SH5.EXE", "C:\\RA\\P.EXE");
    mkfile("C:\\RA\\C.COM", com, sizeof com);
    mkfile("C:\\RA\\X.EXE", mz, sizeof mz);
    mkpe("C:\\RA\\PE.EXE");
    mkfile("C:\\RA\\B.BAT", "ECHO BAT>C:\\RANBAT.TXT\r\n", 24);
    mkfile("C:\\RA\\N", com, sizeof com);
    mkfile("C:\\RA\\A.TXT", com, sizeof com);
    mkfile("C:\\RA\\W.WRI", com, sizeof com);
    mkfile("C:\\RA\\U.ABC", com, sizeof com);
    mkfile("C:\\RA\\U.XYZ", com, sizeof com);
    mkfile("C:\\RA\\M.W1Q", com, sizeof com);
    mkfile("C:\\RA\\R.W1P", com, sizeof com);
    mkfile("C:\\RA\\G.PIF", com, sizeof com);
    /* an extension WIN.INI maps to a missing program; a registered class
       whose program is missing */
    WriteProfileString("Extensions", "w1q", "C:\\NOPE.EXE ^.w1q");
    RegSetValue(HKCR, ".w1p", REG_SZ, "w1pfile", 7);
    RegSetValue(HKCR, "w1pfile\\shell\\open\\command", REG_SZ, "C:\\NOPE2.EXE %1", 15);

    all("exe", "C:\\RA\\P.EXE", "a b");
    all("exe.noext", "C:\\RA\\P", "c");
    all("com", "C:\\RA\\C.COM", NULL);
    all("dosexe", "C:\\RA\\X.EXE", NULL);
    all("pe", "C:\\RA\\PE.EXE", NULL);
    all("bat", "C:\\RA\\B.BAT", NULL);
    all("noext", "C:\\RA\\N", NULL);
    all("txt", "C:\\RA\\A.TXT", NULL);
    all("wri", "C:\\RA\\W.WRI", NULL);
    all("abc", "C:\\RA\\U.ABC", NULL);
    all("abc.args", "C:\\RA\\U.ABC", "x y");
    all("xyz", "C:\\RA\\U.XYZ", NULL);
    all("w1q", "C:\\RA\\M.W1Q", NULL);
    all("w1p", "C:\\RA\\R.W1P", NULL);
    fe("fe.pif", "C:\\RA\\G.PIF");
    all("none.exe", "C:\\RA\\NONE.EXE", NULL);
    all("none.abc", "C:\\RA\\NONE.ABC", NULL);
    all("none.pif", "C:\\RA\\NONE.PIF", NULL);
    all("none.noext", "C:\\RA\\NONE", NULL);
    all("nodir", "C:\\NODIR\\P.EXE", NULL);
    all("dir", "C:\\RA", NULL);
    all("dir.abc", "C:\\RA\\D.ABC", NULL);
    all("exe.lower", "c:\\ra\\p.exe", NULL);
    pump(1000);
    out("SH14 done\r\n");
    mkfile("C:\\SH14.END", "end", 3);
    return failures;
}
