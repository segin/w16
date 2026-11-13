/*
 * sh4.c - SH4: FindExecutable and ShellExecute: where an association
 * comes from (WIN.INI [extensions], the registration database, both),
 * what each returns, and the command line and directory the started
 * program gets (SH5.EXE, which logs them to C:\SH5.TXT). Needs SH5.EXE in
 * C:\ (tests/shellprobe.sh -f build/dos/SH5.EXE=SH5.EXE SH4). A dialog
 * that SHELL may show is logged (window dump, C:\SH4Dn.BMP) and cancelled.
 */
#include <windows.h>

#define TK_REF "sh4ref.h"
#include "tklog.h"
#include "shprobe.h"

#define HKCR ((HKEY)HKEY_CLASSES_ROOT)

static char res[300], buf[600];
static HWND mainw;
static WORD ndialogs;

static void mkdir(const char __far *p);
#pragma aux mkdir = "push ds" "mov ds, dx" "mov dx, ax" "mov ah, 39h" "int 21h" "pop ds" \
    parm [dx ax] modify [ax];

static void mkfile(const char *path)
{
    HFILE f = _lcreat(path, 0);
    _lwrite(f, "w16", 3);
    _lclose(f);
}

static void dump_tree(HWND w, int depth)
{
    char cls[40], text[80];
    RECT r;
    for (; w; w = GetWindow(w, GW_HWNDNEXT)) {
        GetClassName(w, cls, sizeof cls);
        GetWindowText(w, text, sizeof text);
        GetWindowRect(w, &r);
        out(depth ? "  WIN " : "WIN ");
        out(cls);
        out(" \"");
        out(text);
        out("\" ");
        outhex(GetWindowWord(w, GWW_ID));
        out(" ");
        outhex(r.left);
        out(",");
        outhex(r.top);
        out(",");
        outhex(r.right);
        out(",");
        outhex(r.bottom);
        out("\r\n");
        if (!depth)
            dump_tree(GetWindow(w, GW_CHILD), 1);
    }
}

/* a timer, so that a modal dialog SHELL shows inside a call is seen */
void CALLBACK __export Watch(HWND w, UINT m, UINT id, DWORD t)
{
    HWND a = GetActiveWindow();
    (void)w; (void)m; (void)id; (void)t;
    if (a && a != mainw) {
        char cls[40];
        GetClassName(a, cls, sizeof cls);
        if (!lstrcmp(cls, "#32770")) {
            out("DIALOG\r\n");
            dump_tree(a, 0);
            ndialogs++;
            PostMessage(a, WM_COMMAND, IDCANCEL, 0);
        } else if (!lstrcmpi(cls, "Notepad")) {
            out("NOTEPAD\r\n");
            PostMessage(a, WM_CLOSE, 0, 0);
        }
    }
}

/* what SH5 logged since the last call (then delete it) */
static void sh5log(const char *name)
{
    char n[64];
    HFILE f = _lopen("C:\\SH5.TXT", OF_READ);
    UINT k = 0;
    if (f != HFILE_ERROR) {
        k = _lread(f, buf, sizeof buf - 1);
        _lclose(f);
        if (k == (UINT)-1)
            k = 0;
        OpenFile("C:\\SH5.TXT", (OFSTRUCT FAR *)res, OF_DELETE);
    }
    buf[k] = 0;
    if (k >= 2 && buf[k - 2] == '\r')
        buf[k - 2] = 0;
    suffix(n, name, ".run");
    obs_s(n, buf);
}

static void find(const char *name, LPCSTR file, LPCSTR dir)
{
    char n[64];
    UINT r;
    lstrcpy(res, "XXXX");
    r = (UINT)FindExecutable(file, dir, res);
    if (r > 32)
        r = 33;                     /* an instance handle */
    obs(name, r);
    suffix(n, name, ".res");
    obs_s(n, res);
}

static void exec(const char *name, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir)
{
    UINT r = (UINT)ShellExecute(mainw, op, file, params, dir, SW_SHOWMINNOACTIVE);
    if (r > 32)
        r = 33;
    obs(name, r);
    pump(1500);
    sh5log(name);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HINSTANCE inst = (HINSTANCE)init_regs[5];
    HKEY k;

    WaitEvent(0);
    InitApp(inst);
    out("SH4 FindExecutable/ShellExecute\r\n");
    wc.style = 0;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = inst;
    wc.lpszClassName = "SH4";
    RegisterClass(&wc);
    mainw = CreateWindow("SH4", "SH4", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 100,
                         NULL, NULL, inst, NULL);
    SetTimer(mainw, 1, 500, (TIMERPROC)MakeProcInstance((FARPROC)Watch, inst));

    OpenFile("C:\\SH5.TXT", (OFSTRUCT FAR *)res, OF_DELETE);
    /* the documents */
    mkdir("C:\\SHT");
    mkfile("C:\\SHT\\A.TXT");
    mkfile("C:\\SHT\\B.XYZ");
    mkfile("C:\\SHT\\C.W16");
    mkfile("C:\\SHT\\D.W1R");
    mkfile("C:\\SHT\\E.W1B");
    mkfile("C:\\SHT\\F.W1N");
    mkfile("C:\\SHT\\G.W1M");
    mkfile("C:\\SHT\\H.W1C");
    mkfile("C:\\SHT\\I.W1D");
    mkfile("C:\\SHT\\LONGNAME.W16");
    mkfile("C:\\SHT\\J.PIF");
    mkfile("C:\\SHT\\K.BAT");
    mkfile("C:\\SHT\\L.COM");
    mkfile("C:\\SHT\\NOEXT");
    /* associations */
    WriteProfileString("Extensions", "w16", "C:\\SH5.EXE ^.w16");
    WriteProfileString("Extensions", "w1b", "C:\\SH5.EXE inib ^.w1b");
    WriteProfileString("Extensions", "w1m", "C:\\NOPE.EXE ^.w1m");
    WriteProfileString("Extensions", "w1c", "C:\\SH5.EXE nocaret");
    WriteProfileString("Extensions", "w1d", "SH5.EXE ^.w1d");
    RegSetValue(HKCR, ".w1r", REG_SZ, "w1rfile", 7);
    RegSetValue(HKCR, "w1rfile\\shell\\open\\command", REG_SZ, "C:\\SH5.EXE \"%1\" extra", 20);
    RegSetValue(HKCR, "w1rfile\\shell\\print\\command", REG_SZ, "C:\\SH5.EXE /p %1", 16);
    RegSetValue(HKCR, "w1rfile\\shell\\pct\\command", REG_SZ, "C:\\SH5.EXE %1 %2 %% %x", 22);
    RegSetValue(HKCR, ".w1b", REG_SZ, "w1bfile", 7);
    RegSetValue(HKCR, "w1bfile\\shell\\open\\command", REG_SZ, "C:\\SH5.EXE regb %1", 18);
    RegSetValue(HKCR, ".w1n", REG_SZ, "w1nfile", 7);
    RegSetValue(HKCR, "w1nfile", REG_SZ, "Incomplete", 10);
    RegSetValue(HKCR, ".w1m", REG_SZ, "w1mfile", 7);
    RegCreateKey(HKCR, "w1mfile", &k);
    RegCloseKey(k);

    /* ---- FindExecutable ---- */
    find("f.txt", "C:\\SHT\\A.TXT", NULL);
    find("f.xyz", "C:\\SHT\\B.XYZ", NULL);
    find("f.w16", "C:\\SHT\\C.W16", NULL);
    find("f.w1r", "C:\\SHT\\D.W1R", NULL);
    find("f.w1b", "C:\\SHT\\E.W1B", NULL);
    find("f.w1n", "C:\\SHT\\F.W1N", NULL);
    find("f.w1m", "C:\\SHT\\G.W1M", NULL);
    find("f.w1c", "C:\\SHT\\H.W1C", NULL);
    find("f.w1d", "C:\\SHT\\I.W1D", NULL);
    find("f.long", "C:\\SHT\\LONGNAME.W16", NULL);
    find("f.pif", "C:\\SHT\\J.PIF", NULL);
    find("f.bat", "C:\\SHT\\K.BAT", NULL);
    find("f.com", "C:\\SHT\\L.COM", NULL);
    find("f.noext", "C:\\SHT\\NOEXT", NULL);
    find("f.exe", "C:\\SH5.EXE", NULL);
    find("f.exe.rel", "SH5.EXE", "C:\\");
    find("f.notepad", "NOTEPAD.EXE", NULL);
    find("f.missing", "C:\\SHT\\NONE.TXT", NULL);
    find("f.missing.xyz", "C:\\SHT\\NONE.XYZ", NULL);
    find("f.nodir", "C:\\NODIR\\A.TXT", NULL);
    find("f.rel.dir", "A.TXT", "C:\\SHT");
    find("f.rel.dir2", "A.TXT", "C:\\SHT\\");
    find("f.rel.nodir", "A.TXT", NULL);
    find("f.lower", "c:\\sht\\a.txt", NULL);
    find("f.dir", "C:\\SHT", NULL);
    find("f.empty", "", NULL);
    find("f.dot", "C:\\SHT\\A.", NULL);
    find("f.winini", "WIN.INI", NULL);

    /* ---- ShellExecute ---- */
    exec("x.w16", NULL, "C:\\SHT\\C.W16", NULL, NULL);
    exec("x.w16.open", "open", "C:\\SHT\\C.W16", NULL, NULL);
    exec("x.w16.params", NULL, "C:\\SHT\\C.W16", "p1 p2", NULL);
    exec("x.w16.dir", NULL, "C:\\SHT\\C.W16", NULL, "C:\\WINDOWS");
    exec("x.w16.print", "print", "C:\\SHT\\C.W16", NULL, NULL);
    exec("x.w1r", NULL, "C:\\SHT\\D.W1R", NULL, NULL);
    exec("x.w1r.print", "print", "C:\\SHT\\D.W1R", NULL, NULL);
    exec("x.w1r.pct", "pct", "C:\\SHT\\D.W1R", "par", NULL);
    exec("x.w1r.bogus", "bogus", "C:\\SHT\\D.W1R", NULL, NULL);
    exec("x.w1r.OPEN", "OPEN", "C:\\SHT\\D.W1R", NULL, NULL);
    exec("x.w1b", NULL, "C:\\SHT\\E.W1B", NULL, NULL);
    exec("x.w1n", NULL, "C:\\SHT\\F.W1N", NULL, NULL);
    exec("x.w1m", NULL, "C:\\SHT\\G.W1M", NULL, NULL);
    exec("x.w1c", NULL, "C:\\SHT\\H.W1C", NULL, NULL);
    exec("x.w1d", NULL, "C:\\SHT\\I.W1D", NULL, NULL);
    exec("x.long", NULL, "C:\\SHT\\LONGNAME.W16", NULL, NULL);
    exec("x.rel.dir", NULL, "C.W16", NULL, "C:\\SHT");
    exec("x.xyz", NULL, "C:\\SHT\\B.XYZ", NULL, NULL);
    exec("x.missing", NULL, "C:\\SHT\\NONE.W16", NULL, NULL);
    exec("x.exe", NULL, "C:\\SH5.EXE", "a  b", NULL);
    exec("x.exe.print", "print", "C:\\SH5.EXE", "a", NULL);
    exec("x.exe.dir", NULL, "SH5.EXE", "d", "C:\\");
    exec("x.exe.dir2", NULL, "C:\\SH5.EXE", "e", "C:\\SHT");
    exec("x.exe.noext", NULL, "C:\\SH5", "f", NULL);
    exec("x.missing.exe", NULL, "C:\\NONE.EXE", NULL, NULL);
    exec("x.badop.exe", "bogus", "C:\\SH5.EXE", "g", NULL);
    exec("x.txt", NULL, "C:\\SHT\\A.TXT", NULL, NULL);
    exec("x.nodir", NULL, "C:\\SHT\\C.W16", NULL, "C:\\NODIR");
    exec("x.empty", NULL, "", NULL, NULL);
    pump(2000);
    obs("dialogs", ndialogs);
    out("SH4 done\r\n");
    return failures;
}
