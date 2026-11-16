/*
 * sh11.c - SH11: ShellExecute when the associated program is missing: the
 * "Program Not Found" dialog SHELL shows (its texts for several kinds of
 * association), what OK with a directory does (WIN.INI [programs], the
 * program started), what Cancel returns, and a wrong directory. Needs
 * SH5.EXE as C:\SH5.EXE and C:\NOPEDIR\NOPE.EXE (tests/shellprobe.sh -f).
 * A timer answers each dialog: the action for the n-th dialog is acts[n].
 */
#include <windows.h>

#define TK_REF "sh11ref.h"
#include "tklog.h"
#include "shprobe.h"

#define HKCR ((HKEY)HKEY_CLASSES_ROOT)

static HWND mainw;
static HINSTANCE inst;
static int ndlg;
static char buf[300];
/* 'c' cancel, 'o' type the directory then OK, 'k' plain OK, 'w' wrong dir then OK */
static const char *act = "";
static const char *dir = "";

static void mkfile(const char *path)
{
    HFILE f = _lcreat(path, 0);
    _lwrite(f, "w16", 3);
    _lclose(f);
}

void CALLBACK __export Watch(HWND w, UINT m, UINT id, DWORD t)
{
    HWND a = GetActiveWindow();
    char cls[40];
    (void)w; (void)m; (void)id; (void)t;
    if (!a || a == mainw)
        return;
    GetClassName(a, cls, sizeof cls);
    if (lstrcmp(cls, "#32770"))
        return;
    out("DIALOG\r\n");
    dump_win(a, a, 0);
    if (ndlg < 4) {
        lstrcpy(buf, "C:\\SH11D0.BMP");
        buf[8] = (char)('0' + ndlg);
        snap(buf);
    }
    ndlg++;
    switch (*act ? *act++ : 'c') {
    case 'o':
        SetDlgItemText(a, 0xCB, dir);
        PostMessage(a, WM_COMMAND, IDOK, 0);
        break;
    case 'k':
        PostMessage(a, WM_COMMAND, IDOK, 0);
        break;
    default:
        PostMessage(a, WM_COMMAND, IDCANCEL, 0);
        break;
    }
}

static void sh5log(const char *name)
{
    char n[64];
    OFSTRUCT of;
    HFILE f = _lopen("C:\\SH5.TXT", OF_READ);
    UINT k = 0;
    if (f != HFILE_ERROR) {
        k = _lread(f, buf, sizeof buf - 1);
        _lclose(f);
        if (k == (UINT)-1)
            k = 0;
        OpenFile("C:\\SH5.TXT", &of, OF_DELETE);
    }
    buf[k] = 0;
    if (k >= 2 && buf[k - 2] == '\r')
        buf[k - 2] = 0;
    suffix(n, name, ".run");
    obs_s(n, buf);
}

static void exec(const char *name, const char *file, const char *acts, const char *d)
{
    char n[64];
    UINT r;
    out("-- ");
    out(name);
    out("\r\n");
    act = acts;
    dir = d;
    ndlg = 0;
    r = (UINT)ShellExecute(mainw, NULL, file, NULL, NULL, SW_SHOWMINNOACTIVE);
    obs(name, r > 32 ? 33 : r);
    pump(1500);
    suffix(n, name, ".dialogs");
    obs(n, ndlg);
    sh5log(name);
    GetProfileString("programs", "NOPE.EXE", "-", buf, sizeof buf);
    suffix(n, name, ".programs");
    obs_s(n, buf);
}

static void find(const char *name, const char *file)
{
    char n[64];
    UINT r;
    lstrcpy(buf, "XXXX");
    r = (UINT)FindExecutable(file, NULL, buf);
    obs(name, r > 32 ? 33 : r);
    suffix(n, name, ".res");
    obs_s(n, buf);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;

    inst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(inst);
    out("SH11 Program Not Found\r\n");
    wc.style = 0;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = inst;
    wc.lpszClassName = "SH11";
    RegisterClass(&wc);
    mainw = CreateWindow("SH11", "SH11", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 100,
                         NULL, NULL, inst, NULL);
    SetTimer(mainw, 1, 700, (TIMERPROC)MakeProcInstance((FARPROC)Watch, inst));
    mkfile("C:\\A.W1M");
    mkfile("C:\\B.W1P");
    mkfile("C:\\C.W1Q");
    mkfile("C:\\D.W1S");
    mkfile("C:\\E.W1T");
    mkfile("C:\\F.W1U");
    WriteProfileString("Extensions", "w1m", "C:\\NOPE.EXE ^.w1m");
    WriteProfileString("Extensions", "w1p", "NOPE.EXE ^.w1p");
    WriteProfileString("Extensions", "w1q", "C:\\DIR\\NOPE2.EXE ^.w1q");
    WriteProfileString("Extensions", "w1u", "nope5 ^.w1u");
    RegSetValue(HKCR, ".w1s", REG_SZ, "w1sfile", 7);
    RegSetValue(HKCR, "w1sfile\\shell\\open\\command", REG_SZ, "C:\\NOPE3.EXE %1", 15);
    RegSetValue(HKCR, ".w1t", REG_SZ, "w1tfile", 7);
    RegSetValue(HKCR, "w1tfile\\shell\\open\\command", REG_SZ, "NOPE4 %1", 8);

    exec("x.full", "C:\\A.W1M", "c", "");
    exec("x.name", "C:\\B.W1P", "o", "C:\\NOPEDIR");
    exec("x.name.again", "C:\\B.W1P", "c", "");
    find("f.name", "C:\\B.W1P");
    exec("x.dirpath", "C:\\C.W1Q", "wc", "C:\\WRONG");
    exec("x.reg", "C:\\D.W1S", "c", "");
    exec("x.reg.ok", "C:\\D.W1S", "o", "C:\\NOPEDIR");
    GetProfileString("programs", "NOPE3.EXE", "-", buf, sizeof buf);
    obs_s("x.reg.ok.programs3", buf);
    exec("x.reg.again", "C:\\D.W1S", "c", "");
    find("f.reg.again", "C:\\D.W1S");
    exec("x.reg.okfile", "C:\\D.W1S", "o", "C:\\NOPEDIR\\NOPE3.EXE");
    exec("x.reg.noext", "C:\\E.W1T", "k", "");
    exec("x.ini.noext", "C:\\F.W1U", "c", "");
    WriteProfileString("programs", "NOPE.EXE", NULL);
    exec("x.full.ok", "C:\\A.W1M", "o", "C:\\NOPEDIR\\");
    /* a registry command without %1; a value that differs only in case */
    RegSetValue(HKCR, ".w1v", REG_SZ, "w1vfile", 7);
    RegSetValue(HKCR, "w1vfile\\shell\\open\\command", REG_SZ, "C:\\SH5.EXE nopct", 17);
    mkfile("C:\\G.W1V");
    exec("x.nopct", "C:\\G.W1V", "c", "");
    RegSetValue(HKCR, "w1vfile\\shell\\open\\command", REG_SZ, "C:\\SH5.EXE %1", 13);
    exec("x.params", "C:\\G.W1V", "c", "");
    RegSetValue(HKCR, "w16case", REG_SZ, "TXTFILE", 7);
    {
        LONG cb = sizeof buf;
        RegQueryValue(HKCR, "w16case", buf, &cb);
        obs_s("q.case", buf);
    }
    {   /* FindExecutable's value when it succeeds */
        UINT r = (UINT)FindExecutable("C:\\G.W1V", NULL, buf);
        obs("f.raw", r);
        r = (UINT)FindExecutable("C:\\SH5.EXE", NULL, buf);
        obs("f.raw.exe", r);
    }
    pump(1000);
    out("SH11 done\r\n");
    return failures;
}
