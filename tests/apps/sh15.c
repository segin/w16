/*
 * sh15.c - SH15: SHELL.DLL with damaged files, failing writes and odd
 * arguments (the SHELL audit, ARCHITECTURE.md 13.9). SHELL
 * is loaded with LoadLibrary and freed after each section, so that every
 * section starts a new SHELL that reads REG.DAT again; nothing imports it.
 * tests/shellprobe.sh SH15 provides the files (tools/mkshaudit.py) in
 * C:\SHA, SH5.EXE in C:\ and a D: drive with D:\SUB.
 *
 * Sections, by letter (C:\SHASKIP.TXT lists those to leave out):
 *   r  REG.DATs R0-R9 (each: copied over REG.DAT, SHELL loaded, enumerate,
 *      query, set, enumerate; REG.DAT then copied to C:\R0n.DAT)
 *   h  a string freed while a key is open, then 6 KB of new strings
 *      before the key is closed (C:\R10.DAT)
 *   w  writing REG.DAT: through a handle opened before the write; with no
 *      file handle left (C:\R11.DAT), then again (C:\R12.DAT)
 *   i  a database made from nothing, RegEnumKey the first call (R13)
 *   g  REG.DAT deleted after SHELL loaded, before the first call (R14)
 *   n  icons from damaged files
 *   x  ShellExecute with a missing directory, and in a directory of D:
 * and the ones that may end the session, last:
 *   v  ShellExecute with a verb of 70 and 100 characters
 *   p  ShellExecute and WinExec with parameters of 200-600 characters
 *   c  REG.DAT RC (a cycle; R15)       t  REG.DAT RT (R16)
 *   k  SHELL freed with its shell hook set
 *   e  DoEnvironmentSubst(NULL, cb) for several cb
 */
#include <windows.h>

#define TK_REF "sh15ref.h"
#include "tklog.h"
#include "shprobe.h"

#define HKCR ((HKEY)HKEY_CLASSES_ROOT)

typedef LONG (FAR PASCAL *F_OPEN)(HKEY, LPCSTR, HKEY FAR *);
typedef LONG (FAR PASCAL *F_CLOSE)(HKEY);
typedef LONG (FAR PASCAL *F_DEL)(HKEY, LPCSTR);
typedef LONG (FAR PASCAL *F_SET)(HKEY, LPCSTR, DWORD, LPCSTR, DWORD);
typedef LONG (FAR PASCAL *F_QUERY)(HKEY, LPCSTR, LPSTR, LONG FAR *);
typedef LONG (FAR PASCAL *F_ENUM)(HKEY, DWORD, LPSTR, DWORD);
typedef HINSTANCE (FAR PASCAL *F_EXEC)(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, int);
typedef HINSTANCE (FAR PASCAL *F_FIND)(LPCSTR, LPCSTR, LPSTR);
typedef HICON (FAR PASCAL *F_ICON)(HINSTANCE, LPCSTR, UINT);
typedef BOOL (FAR PASCAL *F_HOOK)(HWND, UINT);
typedef DWORD (FAR PASCAL *F_SUBST)(LPSTR, UINT);

static HINSTANCE sh, inst;
static HWND mainw;
static char skip[40], buf[700], res[300], regdat[80], big[700];

static FARPROC fn(const char *name)
{
    FARPROC p = GetProcAddress(sh, name);
    if (!p) {
        out("no ");
        out(name);
        out("\r\n");
    }
    return p;
}
#define RegOpen   ((F_OPEN)fn("RegOpenKey"))
#define RegCreate ((F_OPEN)fn("RegCreateKey"))
#define RegClose  ((F_CLOSE)fn("RegCloseKey"))
#define RegSet    ((F_SET)fn("RegSetValue"))
#define RegQuery  ((F_QUERY)fn("RegQueryValue"))
#define RegEnum   ((F_ENUM)fn("RegEnumKey"))
#define RegDelete ((F_DEL)fn("RegDeleteKey"))

static void load(void)
{
    sh = LoadLibrary("SHELL.DLL");
    if ((UINT)sh < 32)
        out("LoadLibrary SHELL failed\r\n");
}

/* SHELL freed, and gone (no other user keeps it) */
static void unload(const char *tag)
{
    char n[40];
    FreeLibrary(sh);
    suffix(n, tag, ".gone");
    obs(n, GetModuleHandle("SHELL") == NULL);
}

static void del(const char *f)
{
    OFSTRUCT of;
    OpenFile(f, &of, OF_DELETE);
}

static void copy(const char *src, const char *dst)
{
    HFILE i = _lopen(src, OF_READ), o;
    UINT n;
    if (i == HFILE_ERROR) {
        out("copy: no ");
        out(src);
        out("\r\n");
        return;
    }
    o = _lcreat(dst, 0);
    while ((n = _lread(i, buf, sizeof buf)) != 0 && n != (UINT)-1)
        _lwrite(o, buf, n);
    _lclose(o);
    _lclose(i);
}

static int want(char c)
{
    const char *s;
    for (s = skip; *s; s++)
        if (*s == c)
            return 0;
    out("begin ");
    buf[0] = c;
    buf[1] = 0;
    out(buf);
    out("\r\n");
    return 1;
}

static void obs_r(const char *tag, const char *what, LONG r)
{
    char n[48];
    suffix(n, tag, what);
    obs_l(n, r);
}

static void enum0(const char *tag, const char *what)
{
    char n[48];
    LONG r;
    lstrcpy(buf, "X");
    r = RegEnum(HKCR, 0, buf, 80);
    obs_r(tag, what, r);
    suffix(n, tag, what);
    lstrcat(n, ".name");
    obs_s(n, buf);
}

static void query(const char *tag, const char *what, LPCSTR sub)
{
    char n[48];
    LONG r, cb = 80;
    lstrcpy(buf, "X");
    r = RegQuery(HKCR, sub, buf, &cb);
    obs_r(tag, what, r);
    suffix(n, tag, what);
    lstrcat(n, ".text");
    obs_s(n, buf);
}

/* ---- r, c, t: damaged databases ---- */

static void database(char id, const char *snap)
{
    char tag[8], src[20];
    lstrcpy(tag, "r?");
    tag[1] = id;
    lstrcpy(src, "C:\\SHA\\R?.DAT");
    src[8] = id;
    copy(src, regdat);
    load();
    enum0(tag, ".enum");
    {
        char n[48];
        lstrcpy(buf, "X");
        obs_r(tag, ".enum1", RegEnum(HKCR, 1, buf, 80));
        suffix(n, tag, ".enum1.name");
        obs_s(n, buf);
    }
    query(tag, ".q", ".w16");
    query(tag, ".qcmd", "w16file\\shell\\open\\command");
    obs_r(tag, ".set", RegSet(HKCR, "w16new", REG_SZ, "v", 1));
    enum0(tag, ".enum2");
    query(tag, ".qnew", "w16new");
    snapshot(snap);
    /* a good file put back: read now, or the old answer kept? */
    copy("C:\\SHA\\R0.DAT", regdat);
    query(tag, ".retry", ".w16");
    unload(tag);
}

/* ---- h: dead strings and growth with a key open ---- */

static void growth(void)
{
    HKEY h;
    WORD i, k;
    copy("C:\\SHA\\ORIG.DAT", regdat);
    load();
    obs_l("h.create", RegCreate(HKCR, "w16h", &h));
    RegSet(h, "a", REG_SZ, "the first value of a, soon replaced", 0);
    obs_l("h.replace", RegSet(h, "a", REG_SZ, "the second value of a", 0));
    for (i = 0; i < 60; i++) {
        char key[8];
        lstrcpy(key, "k00");
        key[1] = (char)('0' + i / 10);
        key[2] = (char)('0' + i % 10);
        for (k = 0; k < 100; k++)
            big[k] = (char)('A' + (i + k) % 26);
        big[100] = 0;
        big[0] = key[1];
        big[1] = key[2];
        if (RegSet(h, key, REG_SZ, big, 0))
            obsn("h.set", i, 1);
    }
    obs_l("h.close", RegClose(h));
    query("h", ".a", "w16h\\a");
    query("h", ".k59", "w16h\\k59");
    unload("h");
    snapshot("R10.DAT");
}

/* ---- the files in the Windows directory (DOS find first/next) ---- */

static void dos_setdta(void __far *p);
#pragma aux dos_setdta = "push ds" "mov ds, dx" "mov dx, ax" "mov ah, 1Ah" "int 21h" "pop ds" \
    parm [dx ax] modify [ax];
static WORD dos_first(const char __far *p);
#pragma aux dos_first = "push ds" "mov ds, dx" "mov dx, ax" "mov cx, 37h" "mov ah, 4Eh" \
    "int 21h" "sbb ax, ax" "pop ds" parm [dx ax] modify [cx] value [ax];
static WORD dos_next(void);
#pragma aux dos_next = "mov ah, 4Fh" "int 21h" "sbb ax, ax" value [ax];

static char names[160][13], dta[64];
static WORD nnames;

/* the Windows directory's names; with add, the new ones are logged */
static void scan(int add)
{
    WORD i;
    dos_setdta(dta);
    if (dos_first("C:\\WINDOWS\\*.*"))
        return;
    do {
        const char *nm = dta + 30;
        for (i = 0; i < nnames; i++)
            if (!lstrcmp(names[i], nm))
                break;
        if (i == nnames && nnames < 160) {
            lstrcpy(names[nnames++], nm);
            if (add) {
                out("NEW ");
                out(nm);
                out("\r\n");
            }
        }
    } while (!dos_next());
}

/* ---- w: writing REG.DAT ---- */

static void writes(void)
{
    HFILE f[40], h0;
    OFSTRUCT of;
    HKEY h;
    WORD n, i;
    LONG r1, r2, r3, r4, s0, s1, s2, more[8];
    copy("C:\\SHA\\ORIG.DAT", regdat);
    load();
    query("w", ".q", ".txt");                   /* REG.DAT read */
    h0 = OpenFile(regdat, &of, OF_READ | OF_SHARE_DENY_NONE);
    s0 = _llseek(h0, 0, 2);
    obs_l("w.set", RegSet(HKCR, "w16w", REG_SZ, "x", 1));
    s1 = _llseek(h0, 0, 2);
    _lclose(h0);
    h0 = OpenFile(regdat, &of, OF_READ);
    s2 = _llseek(h0, 0, 2);
    _lclose(h0);
    obs("w.grew", s2 > s0);
    obs("w.sameinode", s1 == s2);               /* written in place */
    /* the control: a file rewritten with OpenFile(OF_CREATE) */
    copy("C:\\SHA\\R0.DAT", "C:\\SHA\\T.DAT");
    h0 = OpenFile("C:\\SHA\\T.DAT", &of, OF_READ | OF_SHARE_DENY_NONE);
    f[0] = OpenFile("C:\\SHA\\T.DAT", &of, OF_CREATE | OF_WRITE);
    _lwrite(f[0], "w16", 3);
    _lclose(f[0]);
    obs("w.ctl.sameinode", _llseek(h0, 0, 2) == 3);
    _lclose(h0);
    scan(0);

    /* no handle left: the task's 20 are taken (nothing logged meanwhile) */
    for (n = 0; n < 40; n++) {
        f[n] = OpenFile("C:\\WINDOWS\\WIN.INI", &of, OF_READ | OF_SHARE_DENY_NONE);
        if (f[n] == HFILE_ERROR)
            break;
    }
    r1 = RegSet(HKCR, "w16w2", REG_SZ, "y", 1);
    r2 = RegCreate(HKCR, "w16w3", &h);
    r3 = RegClose(h);
    lstrcpy(res, "X");
    {
        LONG cb = 80;
        r4 = RegQuery(HKCR, "w16w2", res, &cb);
        cb = 80;
        lstrcpy(big, "X");
        more[0] = RegQuery(HKCR, "w16none", big, &cb);
        lstrcpy(big + 100, "X");
        more[1] = RegEnum(HKCR, 0, big + 100, 80);
        more[2] = RegOpen(HKCR, "w16w2", &h);
        more[3] = RegClose(h);
        more[4] = RegDelete(HKCR, "w16none");
        more[5] = RegClose(h);
        more[6] = RegDelete(HKCR, "w16w3");
        more[7] = RegSet(HKCR, "w16w2", REG_SZ, "y", 1);   /* no change */
    }
    for (i = 0; i < n; i++)
        _lclose(f[i]);
    obs_l("w.full.qnone", more[0]);
    obs_s("w.full.qnone.text", big);
    obs_l("w.full.enum", more[1]);
    obs_s("w.full.enum.name", big + 100);
    obs_l("w.full.open", more[2]);
    obs_l("w.full.open.close", more[3]);
    obs_l("w.full.delnone", more[4]);
    obs_l("w.full.close2", more[5]);
    obs_l("w.full.del", more[6]);
    obs_l("w.full.same", more[7]);
    out("OBS ~w.handles=");
    outhex(n);
    out("\r\n");
    obs("w.full", n < 40);
    obs_l("w.full.set", r1);
    obs_l("w.full.create", r2);
    obs_l("w.full.close", r3);
    obs_l("w.full.query", r4);
    obs_s("w.full.query.text", res);
    scan(1);                                    /* anything left behind */
    snapshot("R11.DAT");
    obs_l("w.after", RegSet(HKCR, "w16w4", REG_SZ, "z", 1));
    snapshot("R12.DAT");
    unload("w");
}

/* ---- i: a new database, RegEnumKey first ---- */

static void import_first(void)
{
    del(regdat);
    load();
    pump(2500);
    enum0("i", ".enum");
    snapshot("R13.DAT");
    enum0("i", ".enum2");
    query("i", ".q", "SoundRec");
    unload("i");
}

/* ---- g: REG.DAT gone before the first call ---- */

static void gone(void)
{
    copy("C:\\SHA\\ORIG.DAT", regdat);
    load();
    del(regdat);
    query("g", ".q", ".txt");
    enum0("g", ".enum");
    snapshot("R14.DAT");
    obs_l("g.set", RegSet(HKCR, "w16g", REG_SZ, "x", 1));
    copy("C:\\SHA\\ORIG.DAT", regdat);         /* back: read now? */
    query("g", ".q2", ".txt");
    unload("g");
}

/* ---- n: icons ---- */

static void icon(const char *name, HICON h)
{
    char n[64];
    WORD size;
    if ((UINT)h <= 1) {
        obs(name, (WORD)h);
        return;
    }
    obs(name, 2);                       /* a handle */
    size = (WORD)GlobalSize((HGLOBAL)h);
    suffix(n, name, ".size");
    obs(n, size);
    suffix(n, name, ".sum");
    obs(n, strsum((const char FAR *)GlobalLock((HGLOBAL)h), size));
    GlobalUnlock((HGLOBAL)h);
    DestroyIcon(h);
}

static void iconfile(const char *tag, LPCSTR f)
{
    F_ICON ex = (F_ICON)fn("ExtractIcon");
    char n[40];
    suffix(n, tag, ".count");
    obs(n, (WORD)ex(inst, f, (UINT)-1));
    suffix(n, tag, ".0");
    icon(n, ex(inst, f, 0));
    suffix(n, tag, ".1");
    icon(n, ex(inst, f, 1));
}

static void icons(void)
{
    load();
    iconfile("n.ok", "C:\\SHA\\NEOK.EXE");
    iconfile("n.cut", "C:\\SHA\\NECUT.EXE");
    iconfile("n.cut2", "C:\\SHA\\NECUT2.EXE");
    iconfile("n.cut3", "C:\\SHA\\NECUT3.EXE");
    iconfile("n.icoff", "C:\\SHA\\ICOFF.ICO");
    iconfile("n.icocut", "C:\\SHA\\ICOCUT.ICO");
    unload("n");
}

/* ---- x, v, p: ShellExecute ---- */

static WORD dos_drive(void);
#pragma aux dos_drive = "mov ah, 19h" "int 21h" "xor ah, ah" value [ax];
static void dos_chdir(const char __far *p);
#pragma aux dos_chdir = "push ds" "mov ds, dx" "mov dx, ax" "mov ah, 3Bh" "int 21h" "pop ds" \
    parm [dx ax] modify [ax];
static void dos_getdir(BYTE drive, char __far *p);
#pragma aux dos_getdir = "push ds" "mov ds, cx" "mov ah, 47h" "int 21h" "pop ds" \
    parm [dl] [cx si] modify [ax];

/* drive's current directory (1 = A), with its leading backslash */
static void getdir(BYTE drive, char *p)
{
    p[0] = '\\';
    p[1] = 0;
    dos_getdir(drive, p + 1);
}

/* what SH5 logged since the last call (then deleted) */
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
        del("C:\\SH5.TXT");
    }
    buf[k] = 0;
    if (k >= 2 && buf[k - 2] == '\r')
        buf[k - 2] = 0;
    suffix(n, name, ".run");
    obs_s(n, buf);
}

static void exec(const char *name, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir)
{
    F_EXEC ex = (F_EXEC)fn("ShellExecute");
    UINT r = (UINT)ex(mainw, op, file, params, dir, SW_SHOWMINNOACTIVE);
    if (r > 32)
        r = 33;
    obs(name, r);
    pump(1500);
    sh5log(name);
}

static void execs(void)
{
    F_FIND fx;
    char d[80];
    UINT r;
    load();
    fx = (F_FIND)fn("FindExecutable");
    copy("C:\\SH5.EXE", "C:\\WINDOWS\\SHAX.EXE");
    exec("x.nodir", NULL, "SHAX.EXE", "m1", "C:\\NODIR");
    lstrcpy(res, "X");
    r = (UINT)fx("SHAX.EXE", "C:\\NODIR", res);
    obs("x.find.nodir", r > 32 ? 33 : r);
    obs_s("x.find.nodir.res", res);
    exec("x.nodir.c", NULL, "SH5.EXE", "m2", "C:\\NODIR");
    del("C:\\WINDOWS\\SHAX.EXE");
    /* a directory of the other drive */
    dos_chdir("D:\\");
    exec("x.ddir", NULL, "C:\\SH5.EXE", "d1", "D:\\SUB");
    getdir(4, d);
    obs_s("x.ddir.dcwd", d);
    getdir(3, d);
    obs_s("x.ddir.ccwd", d);
    obs("x.ddir.drive", dos_drive());
    unload("x");
}

static void longverb(void)
{
    WORD i;
    load();
    RegSet(HKCR, ".w15", REG_SZ, "w15file", 0);
    RegSet(HKCR, "w15file\\shell\\open\\command", REG_SZ, "C:\\SH5.EXE %1", 0);
    copy("C:\\SH5.EXE", "C:\\SHA\\A.W15");
    for (i = 0; i < 100; i++)
        big[i] = (char)('a' + i % 26);
    big[70] = 0;
    exec("v.70", big, "C:\\SHA\\A.W15", NULL, NULL);
    big[70] = 'x';
    big[100] = 0;
    exec("v.100", big, "C:\\SHA\\A.W15", NULL, NULL);
    unload("v");
}

/* the longest command line: ShellExecute's and WinExec's results for
   parameters of each length (C:\SH5.EXE and a space before them) */
static const WORD plens[] = { 200, 240, 243, 244, 245, 246, 250, 254, 255, 256, 300,
                              400, 499, 500, 501, 510, 600, 0 };

static void longparams(void)
{
    F_EXEC ex;
    WORD i, k;
    load();
    ex = (F_EXEC)fn("ShellExecute");
    for (k = 0; plens[k]; k++) {
        UINT r;
        for (i = 0; i < plens[k]; i++)
            big[i] = (char)('a' + i % 26);
        big[plens[k]] = 0;
        r = (UINT)ex(mainw, NULL, "C:\\SH5.EXE", big, NULL, SW_SHOWMINNOACTIVE);
        obsn("p.se", plens[k], r > 32 ? 33 : r);
        if (r > 32)
            pump(700);
        lstrcpy(res, "C:\\SH5.EXE ");
        lstrcpy(buf, res);
        lstrcat(buf, big);
        r = WinExec(buf, SW_SHOWMINNOACTIVE);
        obsn("p.we", plens[k], r > 32 ? 33 : r);
        if (r > 32)
            pump(700);
        del("C:\\SH5.TXT");
    }
    unload("p");
}


/* ---- k: SHELL freed while its hook is set ---- */

static void hookfree(void)
{
    HWND w;
    load();
    obs("k.reg", ((F_HOOK)fn("RegisterShellHook"))(mainw, 1));
    unload("k");
    w = CreateWindow("SH15", "SH15b", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 50,
                     NULL, NULL, inst, NULL);
    DestroyWindow(w);
    pump(300);
    obs("k.alive", 1);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HFILE f;
    inst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(inst);
    out("SH15 audit\r\n");
    obs("loaded.at.start", GetModuleHandle("SHELL") != NULL);
    f = _lopen("C:\\SHASKIP.TXT", OF_READ);
    if (f != HFILE_ERROR) {
        _lread(f, skip, sizeof skip - 1);
        _lclose(f);
    }
    GetWindowsDirectory(regdat, sizeof regdat - 10);
    lstrcat(regdat, "\\REG.DAT");
    copy(regdat, "C:\\SHA\\ORIG.DAT");
    wc.style = 0;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = inst;
    wc.lpszClassName = "SH15";
    RegisterClass(&wc);
    mainw = CreateWindow("SH15", "SH15", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 100,
                         NULL, NULL, inst, NULL);
    del("C:\\SH5.TXT");

    if (want('r')) {
        static const char ids[] = "0123456789";
        static char snap[] = "R0?.DAT";
        int i;
        for (i = 0; ids[i]; i++) {
            snap[2] = ids[i];
            database(ids[i], snap);
        }
    }
    if (want('h'))
        growth();
    if (want('w'))
        writes();
    if (want('i'))
        import_first();
    copy("C:\\SHA\\ORIG.DAT", regdat);
    if (want('g'))
        gone();
    copy("C:\\SHA\\ORIG.DAT", regdat);
    if (want('n'))
        icons();
    if (want('x'))
        execs();
    if (want('v'))
        longverb();
    if (want('p'))
        longparams();
    if (want('c'))
        database('C', "R15.DAT");
    if (want('t'))
        database('T', "R16.DAT");
    copy("C:\\SHA\\ORIG.DAT", regdat);
    if (want('k'))
        hookfree();
    if (want('e')) {
        static const UINT cbs[] = { 10, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 100, 10 };
        F_SUBST f;
        WORD i;
        load();
        f = (F_SUBST)fn("DoEnvironmentSubst");
        for (i = 0; i < 13; i++) {
            DWORD r = f(NULL, cbs[i]);
            obsn("e.null", i, LOWORD(r));
            obsn("e.null.hi", i, HIWORD(r));
        }
        unload("e");
    }
    out("SH15 done\r\n");
    return failures;
}
