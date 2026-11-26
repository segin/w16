/*
 * tk69.c - where KERNEL looks for the DLLs a program imports when the
 * program is in a directory of its own, what LoadModule makes of files
 * that are no Windows programs (OS/2's NE and LX files and other NE
 * targets), and KERNEL's constant exports (__F000H ...). Run as the shell
 * (tests/dropin.sh -i 'boot:shell=C:\TK69.EXE'; the boot-test case
 * kexedir) with C:\TKID.TPL (tests/apps/tkid.c, module TKID, ID_GET),
 * C:\TK69X\TK69A.EXE (tests/apps/tk69a.c, imports TKID.ID_GET and writes
 * the copy's id to C:\TK69.ID) and C:\TK69X\TK69L.DLL (tkid.c as module
 * TK69L, importing TKID). Writes C:\TK69.LOG, one line per result.
 *
 * - "order": copies of TKID.DLL with ids 1-5 in TK69A's own directory
 *   (C:\TK69X), the current directory (C:\TK69CC), the Windows directory,
 *   the system directory and this program's directory (C:\); WinExec of
 *   C:\TK69X\TK69A.EXE, the copy found deleted, five times: the order.
 * - "named": only C:\TK69X's copy, TK69A named several ways (relative,
 *   "..", from the root, lower case) from several current directories;
 *   "sub": the same for C:\TK69X\SUB\TK69A.EXE.
 * - "library": only C:\TK69X's copy, LoadLibrary("C:\TK69X\TK69L.DLL").
 * - "foreign": LoadModule (and for NE targets 1 and 3 WinExec) of
 *   copies of TK69A.EXE whose NE header says another target system (byte
 *   36h: 0, 1 OS/2, 3, 4, 5; LoadLibrary of the OS/2 one too), of files
 *   with "LX", "LE" and "PE\0\0" where the NE header would be, and of an
 *   OS/2 1.x program (target 1, expected version 0).
 * - "const": GetProcAddress of KERNEL's constant exports by ordinal, the
 *   high word, and the low word's selector base and limit.
 * Left out: WinExec of the target-4 file returned an instance on 3.1 and
 * ran no TK69A (presumably WINOLDAP: a wait for module WINOLDAP to go did
 * not end with TK69 as the shell).
 * Windows 3.1 also missed TK69A's directory from C:\TK69C (the
 * name in full, relative, by ".." or from the root) but not from
 * C:\TK69CC, C:\ABCDE or C:\TK6 (kernel/loader.c, prog_dir), a quirk w16
 * does not copy.
 * (StarOffice 3.1's programs import DLLs from their own directory, and
 * its CD holds OS/2 programs.)
 */
#include <windows.h>

static char line[200];
static int ln;

static void put(const char *s)
{
    while (*s)
        line[ln++] = *s++;
}

static void hex(DWORD v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--)
        line[ln++] = "0123456789ABCDEF"[(WORD)(v >> (4 * i)) & 15];
}

static void flush(void)
{
    HFILE f = _lopen("C:\\TK69.LOG", OF_READWRITE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\TK69.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    line[ln++] = '\r';
    line[ln++] = '\n';
    _lwrite(f, line, ln);
    _lclose(f);
    ln = 0;
}

extern int __cdecl dos_int21(WORD FAR *r);

/* INT 21h AH=39h/3Ah/3Bh on a path (DS:DX) */
static void dpath(WORD ax, const char FAR *p)
{
    WORD r[8];
    int i;
    for (i = 0; i < 8; i++)
        r[i] = 0;
    r[0] = ax;
    r[3] = (WORD)(DWORD)p;
    r[6] = (WORD)((DWORD)p >> 16);
    dos_int21((WORD FAR *)r);
}

static BYTE buf[0x6000];
static OFSTRUCT ofs;

static WORD read_file(const char *name)
{
    HFILE f = _lopen(name, OF_READ);
    WORD n;
    if (f == HFILE_ERROR)
        return 0;
    n = _lread(f, buf, sizeof buf);
    _lclose(f);
    return n == (WORD)-1 ? 0 : n;
}

static void write_file(const char *name, WORD n)
{
    HFILE f = _lcreat(name, 0);
    if (f != HFILE_ERROR) {
        _lwrite(f, buf, n);
        _lclose(f);
    }
}

static char dirs[6][80];
static const char *const dname[6] = { "", "exedir", "cwd", "windir", "sysdir", "appdir" };

static void path_of(char *d, const char *dir, const char *file)
{
    int i = 0, k = 0;
    while (dir[i])
        d[k++] = dir[i++];
    if (k && d[k - 1] != '\\')
        d[k++] = '\\';
    for (i = 0; file[i]; i++)
        d[k++] = file[i];
    d[k] = 0;
}

static void wait_gone(const char *mod)
{
    int i;
    MSG m;
    for (i = 0; i < 30000 && GetModuleHandle(mod); i++) {
        PeekMessage(&m, 0, 0, 0, PM_REMOVE);
        Yield();
    }
}

/* the id TK69A.EXE wrote, 0 if none */
static int child_id(void)
{
    char c[2];
    HFILE f = _lopen("C:\\TK69.ID", OF_READ);
    int n;
    if (f == HFILE_ERROR)
        return 0;
    n = _lread(f, c, 1);
    _lclose(f);
    OpenFile("C:\\TK69.ID", &ofs, OF_DELETE);
    return n == 1 ? c[0] - '0' : 0;
}

static void copies(WORD tpl, int mark, int from, int to)
{
    char p[100];
    int i;
    for (i = from; i <= to; i++) {
        buf[mark] = (BYTE)i;
        path_of(p, dirs[i], "TKID.DLL");
        write_file(p, tpl);
    }
}

static void del_copy(int i)
{
    char p[100];
    OFSTRUCT of;
    path_of(p, dirs[i], "TKID.DLL");
    OpenFile(p, &of, OF_DELETE);
}

static void run_child(const char *cmd)
{
    WORD r = WinExec(cmd, SW_SHOWNORMAL);
    int id;
    wait_gone("TK69A");
    id = child_id();
    put(" result=");
    hex(r < 32 ? r : 0xFFFF, 4);
    put(" found=");
    put(id >= 1 && id <= 5 ? dname[id] : "none");
    flush();
    if (id >= 1 && id <= 5)
        del_copy(id);
}

/* a copy of TK69A.EXE with its new header changed */
static void foreign(const char *what, WORD n, WORD ne, int off, const BYTE *bytes, int nb)
{
    static BYTE save[8];
    WORD shw[2];
    struct { WORD env; LPSTR cmd; WORD FAR *show; WORD FAR *res; } lp;
    static char tail[] = "\0\r";
    WORD r;
    int i;
    for (i = 0; i < nb; i++) {
        save[i] = buf[ne + off + i];
        buf[ne + off + i] = bytes[i];
    }
    write_file("C:\\TK69X\\TK69F.EXE", n);
    for (i = 0; i < nb; i++)
        buf[ne + off + i] = save[i];
    shw[0] = 2;
    shw[1] = SW_SHOWNORMAL;
    lp.env = 0;
    lp.cmd = tail;
    lp.show = shw;
    lp.res = NULL;
    r = LoadModule("C:\\TK69X\\TK69F.EXE", &lp);
    wait_gone("TK69A");
    child_id();
    put("foreign ");
    put(what);
    put(" LoadModule=");
    hex(r < 32 ? r : 0xFFFF, 4);
    if (what[0] == 'o' && what[2] == '=' && (what[3] == '1' || what[3] == '3')) {
        /* and WinExec (an NE file: not WINOLDAP's) */
        r = WinExec("C:\\TK69X\\TK69F.EXE", SW_SHOWNORMAL);
        wait_gone("TK69A");
        put(" WinExec=");
        hex(r < 32 ? r : 0xFFFF, 4);
        put(" ran=");
        put(child_id() ? "1" : "0");
    }
    flush();
}

static const struct { const char *cwd, *cmd; } vars[] = {
    { "C:\\", "TK69X\\TK69A.EXE" },
    { "C:\\", "C:\\TK69X\\TK69A.EXE" },
    { "C:\\TK69X", "TK69A.EXE" },
    { "C:\\TK69CC", "C:\\TK69X\\TK69A.EXE" },
    { "C:\\TK69CC", "..\\TK69X\\TK69A.EXE" },
    { "C:\\TK69CC", "\\TK69X\\TK69A.EXE" },
    { "C:\\TK69CC", "c:\\tk69x\\tk69a.exe" },
    { "C:\\ABCDE", "C:\\TK69X\\TK69A.EXE" },
    { "C:\\TK6", "C:\\TK69X\\TK69A.EXE" },
    { NULL, NULL }
};

static const WORD kord[] = { 113, 114, 173, 174, 178, 179, 181, 182, 183,
                             190, 193, 194, 195, 0 };

int __cdecl tmain(void)
{
    WORD tpl, n, ne;
    int mark = -1, i;
    char p[100];

    put("T start mode=");
    put(GetWinFlags() & WF_ENHANCED ? "E" : "S");
    flush();

    /* the constant exports */
    {
        HMODULE k = GetModuleHandle("KERNEL");
        for (i = 0; kord[i]; i++) {
            DWORD a = (DWORD)GetProcAddress(k, MAKEINTRESOURCE(kord[i]));
            WORD v = LOWORD(a);
            put("const ");
            hex(kord[i], 3);
            put(" hi=");
            hex(HIWORD(a), 4);
            if (kord[i] == 113 || kord[i] == 114) {
                put(" value=");
                hex(v, 4);
            } else if (kord[i] == 178) {
                put(" same as GetWinFlags=");
                put(v == (WORD)GetWinFlags() ? "1" : "0");
            } else {
                put(" base=");
                hex(GetSelectorBase(v), 8);
                put(" limit=");
                hex(GetSelectorLimit(v), 8);
                put(" rpl=");
                hex(v & 3, 1);
                put(" ldt=");
                hex((v >> 2) & 1, 1);
            }
            flush();
        }
    }

    tpl = read_file("C:\\TKID.TPL");
    for (n = 0; n + 6 <= tpl; n++)
        if (buf[n] == 'T' && buf[n + 1] == 'K' && buf[n + 2] == 'I' &&
            buf[n + 3] == 'D' && buf[n + 4] == '#') {
            mark = n + 5;
            break;
        }
    put("T template=");
    put(mark >= 0 ? "1" : "0");
    flush();
    if (mark < 0)
        return 1;
    lstrcpy(dirs[1], "C:\\TK69X");
    lstrcpy(dirs[2], "C:\\TK69CC");
    GetWindowsDirectory(dirs[3], sizeof dirs[3]);
    GetSystemDirectory(dirs[4], sizeof dirs[4]);
    lstrcpy(dirs[5], "C:\\");
    dpath(0x3900, "C:\\TK69C");
    dpath(0x3900, "C:\\TK69CC");
    dpath(0x3900, "C:\\ABCDE");
    dpath(0x3900, "C:\\TK6");

    /* the order */
    copies(tpl, mark, 1, 5);
    dpath(0x3B00, "C:\\TK69CC");
    for (i = 0; i < 5; i++) {
        put("order ");
        hex(i + 1, 1);
        run_child("C:\\TK69X\\TK69A.EXE");
    }
    for (i = 1; i <= 5; i++)
        del_copy(i);

    /* only the copy in TK69A's directory, the program named several ways
       from several current directories */
    copies(tpl, mark, 1, 1);
    for (i = 0; vars[i].cwd; i++) {
        dpath(0x3B00, vars[i].cwd);
        put("named ");
        put(vars[i].cmd);
        put(" from ");
        put(vars[i].cwd);
        run_child(vars[i].cmd);
        copies(tpl, mark, 1, 1);
    }
    del_copy(1);
    /* a program in a subdirectory of the current directory */
    {
        char p2[100];
        WORD m;
        path_of(p, dirs[1], "TK69A.EXE");
        m = read_file(p);
        dpath(0x3900, "C:\\TK69X\\SUB");
        write_file("C:\\TK69X\\SUB\\TK69A.EXE", m);
        tpl = read_file("C:\\TKID.TPL");
        lstrcpy(dirs[1], "C:\\TK69X\\SUB");
        copies(tpl, mark, 1, 1);
        dpath(0x3B00, "C:\\TK69X");
        put("sub C:\\TK69X\\SUB\\TK69A.EXE from C:\\TK69X:");
        run_child("C:\\TK69X\\SUB\\TK69A.EXE");
        copies(tpl, mark, 1, 1);
        dpath(0x3B00, "C:\\TK69C");
        put("sub C:\\TK69X\\SUB\\TK69A.EXE from C:\\TK69C:");
        run_child("C:\\TK69X\\SUB\\TK69A.EXE");
        del_copy(1);
        OpenFile("C:\\TK69X\\SUB\\TK69A.EXE", &ofs, OF_DELETE);
        lstrcpy(dirs[1], "C:\\TK69X");
        (void)p2;
    }
    dpath(0x3B00, "C:\\");

    /* a DLL in its own directory, loaded by path, importing TKID */
    copies(tpl, mark, 1, 1);
    {
        HINSTANCE h = LoadLibrary("C:\\TK69X\\TK69L.DLL");
        put("library LoadLibrary=");
        hex((WORD)h < 32 ? (WORD)h : 0xFFFF, 4);
        put(" TKID loaded=");
        put(GetModuleHandle("TKID") ? "1" : "0");
        flush();
        if ((WORD)h >= 32)
            FreeLibrary(h);
    }
    del_copy(1);

    /* files that are no Windows programs (TKID found in the Windows
       directory: TK69A itself loads) */
    copies(tpl, mark, 3, 3);
    path_of(p, dirs[1], "TK69A.EXE");
    n = read_file(p);
    ne = n > 0x40 ? *(WORD *)(buf + 0x3C) : 0;
    put("T child=");
    hex(n, 4);
    put(" ne=");
    hex(ne, 4);
    put(" sig=");
    put(ne && ne + 0x40 < n && buf[ne] == 'N' && buf[ne + 1] == 'E' ? "NE" : "?");
    flush();
    if (ne && ne + 0x40 < n) {
        static const BYTE os0[] = { 0 }, os1[] = { 1 }, os3[] = { 3 },
                          os4[] = { 4 }, os5[] = { 5 }, ver0[] = { 0, 0 },
                          lx[] = { 'L', 'X' }, le[] = { 'L', 'E' },
                          pe[] = { 'P', 'E', 0, 0 };
        foreign("same", n, ne, 0x36, os1 + 0, 0);
        foreign("os=0", n, ne, 0x36, os0, 1);
        {
            /* LoadLibrary of the OS/2 file */
            HINSTANCE h;
            buf[ne + 0x36] = 1;
            write_file("C:\\TK69X\\TK69F.EXE", n);
            buf[ne + 0x36] = 2;
            h = LoadLibrary("C:\\TK69X\\TK69F.EXE");
            put("foreign os=1 LoadLibrary=");
            hex((WORD)h < 32 ? (WORD)h : 0xFFFF, 4);
            flush();
            if ((WORD)h >= 32)
                FreeLibrary(h);
        }
        foreign("os=1", n, ne, 0x36, os1, 1);
        foreign("os=3", n, ne, 0x36, os3, 1);
        foreign("os=4", n, ne, 0x36, os4, 1);
        foreign("os=5", n, ne, 0x36, os5, 1);
        foreign("LX", n, ne, 0, lx, 2);
        foreign("LE", n, ne, 0, le, 2);
        foreign("PE", n, ne, 0, pe, 4);
        {
            /* OS/2 1.x: target 1 and expected version 0 */
            BYTE s = buf[ne + 0x36];
            buf[ne + 0x36] = 1;
            foreign("os=1 ver=0", n, ne, 0x3E, ver0, 2);
            buf[ne + 0x36] = s;
        }
    }
    del_copy(3);
    OpenFile("C:\\TK69X\\TK69F.EXE", &ofs, OF_DELETE);
    put("T done");
    flush();
    return 0;
}
