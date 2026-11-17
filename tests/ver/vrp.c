/*
 * vrp.c - VRP.EXE, the VER.DLL probe (ARCHITECTURE.md, VER): calls every
 * function of VER.DLL on the files tests/ver/mkinputs.py makes (C:\VT,
 * D:\VT) and logs each call, its result, the buffers it filled and what
 * it did to the files, to C:\VRP.LOG. Run as the shell on the reference
 * 3.1 with Microsoft's VER.DLL and with w16's (tests/ver/vrun.sh); the
 * logs are compared section by section (tests/ver/vcmp.py).
 *
 * Sections ("== NAME"): exports, resource (GetFileResourceSize,
 * GetFileResource), size (GetFileVersionInfoSize, GetFileVersionInfo on
 * every file of C:\VT\LIST.TXT), query (VerQueryValue paths on blocks
 * read by GetFileVersionInfo and on raw blocks), language
 * (VerLanguageName), find (VerFindFile), install (VerInstallFile),
 * modules (the files named in C:\VT\MODS.TXT, if any: their versions).
 * C:\VRP.ARG (the shell has no arguments) names the sections to run
 * (letters: e r s q l f i m; default all).
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 2 (the Ver*
 * functions, GetFileResource*, OpenFile), Vol. 1 ch. 9 (installation);
 * MS-DOS's INT 21h functions for the file listings.
 */
#include <windows.h>
#include <ver.h>

static const char logname[] = "C:\\VRP.LOG";
static char line[700];
static BYTE __far *buf;                 /* 8192 bytes */
static BYTE __far *buf2;                /* 8192 bytes */
static int hasd;                        /* D: exists */

/* no C runtime (-zl) */
static void fill(void FAR *d, int c, unsigned n)
{
    BYTE FAR *p = d;
    while (n--)
        *p++ = (BYTE)c;
}

static void put(const char FAR *s, int n)
{
    HFILE f = _lopen(logname, OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat(logname, 0);
    else
        _llseek(f, 0, 2);
    _lwrite(f, s, n);
    _lclose(f);
}

static void lg(const char *fmt, ...)
{
    int n = wvsprintf(line, fmt, (LPSTR)(&fmt + 1));
    line[n++] = '\r';
    line[n++] = '\n';
    put(line, n);
}

/* a summary of bytes: count, rotating sum, the first and last bytes */
static char *summ(const BYTE FAR *p, long n)
{
    static char ss[3][100];
    static int k3;
    char *s = ss[k3++ % 3];
    WORD sum = 0;
    long i;
    int k, j;
    if (n <= 0) {
        wsprintf(s, "-");
        return s;
    }
    for (i = 0; i < n; i++)
        sum = ((sum << 1) | (sum >> 15)) + p[i];
    k = wsprintf(s, "%ld sum=%04X [", n, sum);
    for (j = 0; j < 8 && j < n; j++)
        k += wsprintf(s + k, "%02X", p[j]);
    if (n > 8) {
        k += wsprintf(s + k, "..");
        for (j = (n > 12 ? (int)n - 4 : 8); j < n; j++)
            k += wsprintf(s + k, "%02X", p[j]);
    }
    wsprintf(s + k, "]");
    return s;
}

/* a string as it is, with non-printable bytes as \xx, at most n bytes */
static char *shows(const BYTE FAR *p, int n)
{
    static char s[300];
    int k = 0, i;
    for (i = 0; i < n && k < 280; i++) {
        if (p[i] >= 0x20 && p[i] < 0x7F && p[i] != '\\')
            s[k++] = p[i];
        else
            k += wsprintf(s + k, "\\%02X", p[i]);
    }
    s[k] = 0;
    return s;
}

/* ---- MS-DOS calls (INT 21h; DOSX and WIN386 pass them on) ---- */
static int dos_getattr(const char FAR *p)
{
    int r = 0, cf = 0;
    _asm {
        push ds
        lds dx, p
        mov ax, 4300h
        int 21h
        pop ds
        sbb bx, bx
        mov cf, bx
        mov r, cx
    }
    return cf ? -1 : r;
}

static int dos_setattr(const char FAR *p, int a)
{
    int cf = 0;
    _asm {
        push ds
        lds dx, p
        mov cx, a
        mov ax, 4301h
        int 21h
        pop ds
        sbb bx, bx
        mov cf, bx
    }
    return cf ? -1 : 0;
}

static int dos_mkdir(const char FAR *p)
{
    int cf = 0;
    _asm {
        push ds
        lds dx, p
        mov ah, 39h
        int 21h
        pop ds
        sbb bx, bx
        mov cf, bx
    }
    return cf ? -1 : 0;
}

static int dos_rmdir(const char FAR *p)
{
    int cf = 0;
    _asm {
        push ds
        lds dx, p
        mov ah, 3Ah
        int 21h
        pop ds
        sbb bx, bx
        mov cf, bx
    }
    return cf ? -1 : 0;
}

static int dos_chdir(const char FAR *p)
{
    int cf = 0;
    _asm {
        push ds
        lds dx, p
        mov ah, 3Bh
        int 21h
        pop ds
        sbb bx, bx
        mov cf, bx
    }
    return cf ? -1 : 0;
}

static int dos_unlink(const char FAR *p)
{
    int cf = 0;
    _asm {
        push ds
        lds dx, p
        mov ah, 41h
        int 21h
        pop ds
        sbb bx, bx
        mov cf, bx
    }
    return cf ? -1 : 0;
}

static void dos_setdta(void FAR *p)
{
    _asm {
        push ds
        lds dx, p
        mov ah, 1Ah
        int 21h
        pop ds
    }
}

static int dos_find(const char FAR *p, int first)
{
    int cf = 0;
    if (first) {
        _asm {
            push ds
            lds dx, p
            mov cx, 16h
            mov ah, 4Eh
            int 21h
            pop ds
            sbb bx, bx
            mov cf, bx
        }
    } else {
        _asm {
            mov ah, 4Fh
            int 21h
            sbb bx, bx
            mov cf, bx
        }
    }
    return cf ? -1 : 0;
}

static void dos_getdate(HFILE h, WORD *t, WORD *d)
{
    WORD tt = 0, dd = 0;
    _asm {
        mov bx, h
        mov ax, 5700h
        int 21h
        jc no
        mov tt, cx
        mov dd, dx
    no:
    }
    *t = tt;
    *d = dd;
}

static void dos_setdate(HFILE h, WORD t, WORD d)
{
    _asm {
        mov bx, h
        mov cx, t
        mov dx, d
        mov ax, 5701h
        int 21h
    }
}

#define FIXTIME ((3 << 11) | (10 << 5))
#define FIXDATE (((1992 - 1980) << 9) | (3 << 5) | 10)

static BYTE dta[64];

/* the name of every file in DIR (a pattern), into names (NUL-separated) */
static int dirnames(const char *pat, char *names, int max)
{
    int n = 0, k = 0;
    dos_setdta(dta);
    if (dos_find(pat, 1) == 0) {
        do {
            if (!(dta[21] & 0x10) && k + 14 < max) {
                lstrcpy(names + k, (LPSTR)(dta + 30));
                k += lstrlen(names + k) + 1;
                n++;
            }
        } while (dos_find(pat, 0) == 0);
    }
    names[k] = 0;
    return n;
}

static void fixdates(const char *dir)
{
    char names[2000], p[100], *q;
    HFILE h;
    wsprintf(p, "%s\\*.*", (LPSTR)dir);
    dirnames(p, names, sizeof names);
    for (q = names; *q; q += lstrlen(q) + 1) {
        wsprintf(p, "%s\\%s", (LPSTR)dir, (LPSTR)q);
        h = _lopen(p, OF_READ);
        if (h != HFILE_ERROR) {
            dos_setdate(h, FIXTIME, FIXDATE);
            _lclose(h);
        }
    }
}

/* list a directory: name, size, attributes, contents, date */
static void listdir(const char *dir)
{
    char names[2000], p[100], *q;
    HFILE h;
    long n;
    int a, got;
    wsprintf(p, "%s\\*.*", (LPSTR)dir);
    dirnames(p, names, sizeof names);
    for (q = names; *q; q += lstrlen(q) + 1) {
        if (!lstrcmp(q, "KEEP"))
            continue;
        wsprintf(p, "%s\\%s", (LPSTR)dir, (LPSTR)q);
        a = dos_getattr(p);
        h = _lopen(p, OF_READ | OF_SHARE_DENY_NONE);
        if (h == HFILE_ERROR) {
            lg("    %s: %s a=%02X (cannot open)", (LPSTR)dir, (LPSTR)q, a);
            continue;
        }
        n = _llseek(h, 0, 2);
        _llseek(h, 0, 0);
        got = _lread(h, buf2, n > 8000 ? 8000 : (int)n);
        _lclose(h);
        lg("    %s: %s a=%02X %s", (LPSTR)dir, (LPSTR)q, a, (LPSTR)summ(buf2, got));
        if (n > 8000)
            lg("      size %ld", n);
    }
}

static void cleandir(const char *dir)
{
    char names[2000], p[100], *q;
    wsprintf(p, "%s\\*.*", (LPSTR)dir);
    dirnames(p, names, sizeof names);
    for (q = names; *q; q += lstrlen(q) + 1) {
        if (!lstrcmp(q, "KEEP"))
            continue;
        wsprintf(p, "%s\\%s", (LPSTR)dir, (LPSTR)q);
        dos_setattr(p, 0);
        dos_unlink(p);
    }
}

/* copy a file, keeping its date */
static int copyf(const char *from, const char *to)
{
    HFILE a, b;
    int n;
    WORD t, d;
    a = _lopen(from, OF_READ);
    if (a == HFILE_ERROR)
        return -1;
    b = _lcreat(to, 0);
    if (b == HFILE_ERROR) {
        _lclose(a);
        return -1;
    }
    while ((n = _lread(a, buf2, 8000)) > 0)
        _lwrite(b, buf2, n);
    dos_getdate(a, &t, &d);
    dos_setdate(b, t, d);
    _lclose(a);
    _lclose(b);
    return 0;
}

/* POOL\name.DLL to dir\as */
static void place(const char *poolname, const char *dir, const char *as)
{
    char a[100], b[100];
    wsprintf(a, "C:\\VT\\POOL\\%s", (LPSTR)poolname);
    wsprintf(b, "%s\\%s", (LPSTR)dir, (LPSTR)as);
    if (copyf(a, b))
        lg("  (setup: cannot copy %s to %s)", (LPSTR)a, (LPSTR)b);
}

/* ---- exports ---- */
static void t_exports(void)
{
    static const char *const names[] = {
        "WEP", "GETFILERESOURCESIZE", "GETFILERESOURCE", "GETFILEVERSIONINFOSIZE",
        "GETFILEVERSIONINFO", "VERFINDFILE", "VERINSTALLFILE", "VERLANGUAGENAME",
        "VERQUERYVALUE", 0 };
    HMODULE m = GetModuleHandle("VER");
    int i;
    lg("== exports");
    lg(" GetModuleHandle(VER) %s", (LPSTR)(m ? "ok" : "0"));
    for (i = 0; names[i]; i++)
        lg(" %s: %s", (LPSTR)names[i], (LPSTR)(GetProcAddress(m, names[i]) ? "yes" : "no"));
    for (i = 1; i <= 11; i++)
        lg(" ordinal %d: %s", i, (LPSTR)(GetProcAddress(m, MAKEINTRESOURCE(i)) ? "yes" : "no"));
}

/* ---- GetFileResourceSize, GetFileResource ---- */
static void res1(const char *file, LPCSTR type, LPCSTR id, const char *what)
{
    DWORD off = 0xDEADBEEFL, n, n2;
    BOOL r;
    char p[100];
    wsprintf(p, "C:\\VT\\%s", (LPSTR)file);
    n = GetFileResourceSize(p, type, id, &off);
    lg(" GetFileResourceSize(%s, %s) = %ld off %08lX", (LPSTR)file, (LPSTR)what, n, off);
    if (!n)
        return;
    fill(buf, 0xAA, (unsigned)n + 16);
    r = GetFileResource(p, type, id, off, n, buf);
    lg("  GetFileResource(off) = %d %s; after: %s", r, (LPSTR)summ(buf, n),
       (LPSTR)summ(buf + (unsigned)n, 4));
    fill(buf, 0xAA, (unsigned)n + 16);
    r = GetFileResource(p, type, id, 0, n, buf);
    lg("  GetFileResource(0) = %d %s", r, (LPSTR)summ(buf, n));
    n2 = n > 10 ? 10 : 1;
    fill(buf, 0xAA, (unsigned)n + 16);
    r = GetFileResource(p, type, id, off, n2, buf);
    lg("  GetFileResource(off, %ld) = %d %s", n2, r, (LPSTR)summ(buf, n2 + 4));
    fill(buf, 0xAA, (unsigned)n + 16);
    r = GetFileResource(p, type, id, off + 2, n, buf);
    lg("  GetFileResource(off+2) = %d %s", r, (LPSTR)summ(buf, n));
    fill(buf, 0xAA, (unsigned)n + 16);
    r = GetFileResource(p, type, id, 0, n + 8, buf);
    lg("  GetFileResource(0, n+8) = %d %s", r, (LPSTR)summ(buf, n + 8));
    fill(buf, 0xAA, (unsigned)n + 16);
    r = GetFileResource(p, type, id, 0, 0, buf);
    lg("  GetFileResource(0, 0) = %d %s", r, (LPSTR)summ(buf, 4));
}

static void t_res(void)
{
    lg("== resource");
    res1("GOOD.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("GOOD0.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("GOOD.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(2), "16,2");
    res1("GOOD.DLL", MAKEINTRESOURCE(10), MAKEINTRESOURCE(1), "10,1");
    res1("GOOD.DLL", "#16", "#1", "#16,#1");
    res1("NAMED.DLL", MAKEINTRESOURCE(16), "VS_VERSION_INFO", "16,VS_VERSION_INFO");
    res1("NAMED.DLL", MAKEINTRESOURCE(16), "vs_version_info", "16,vs_version_info");
    res1("NAMED.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("ID2.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(2), "16,2");
    res1("ID2.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("STRTYPE.DLL", "VERSION", MAKEINTRESOURCE(1), "VERSION,1");
    res1("STRTYPE.DLL", "version", MAKEINTRESOURCE(1), "version,1");
    res1("MIXED.DLL", MAKEINTRESOURCE(10), MAKEINTRESOURCE(5), "10,5");
    res1("MIXED.DLL", MAKEINTRESOURCE(7), "FONTDIR", "7,FONTDIR");
    res1("MIXED.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("MIXED.DLL", MAKEINTRESOURCE(6), MAKEINTRESOURCE(1), "6,1");
    res1("TWO.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("TWO.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(2), "16,2");
    res1("TBLBIG.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("TBLSMALL.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("OW.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("NONE.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("NOVER.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("NOVER.DLL", MAKEINTRESOURCE(10), MAKEINTRESOURCE(1), "10,1");
    res1("PLAIN.TXT", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("STUB.EXE", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("SHORT.EXE", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("EMPTY.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("MISSING.DLL", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
    res1("GOOD.DL_", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), "16,1");
}

/* ---- GetFileVersionInfoSize, GetFileVersionInfo ---- */
static void size1(const char *p, const char *shown)
{
    DWORD h = 0xDEADBEEFL, n;
    BOOL r;
    n = GetFileVersionInfoSize(p, &h);
    lg(" %s: size %ld handle %08lX", (LPSTR)shown, n, h);
    if (n) {
        fill(buf, 0xAA, (unsigned)n + 16);
        r = GetFileVersionInfo(p, h, n, buf);
        lg("  GFVI(h, n) = %d %s; after %s", r, (LPSTR)summ(buf, n),
           (LPSTR)summ(buf + (unsigned)n, 8));
        fill(buf, 0xAA, (unsigned)n + 16);
        r = GetFileVersionInfo(p, 0, n, buf);
        lg("  GFVI(0, n) = %d %s", r, (LPSTR)summ(buf, n));
        fill(buf, 0xAA, (unsigned)n + 16);
        r = GetFileVersionInfo(p, h, n - 9, buf);
        lg("  GFVI(h, n-9) = %d %s", r, (LPSTR)summ(buf, n));
        fill(buf, 0xAA, (unsigned)n + 16);
        r = GetFileVersionInfo(p, h, n + 12, buf);
        lg("  GFVI(h, n+12) = %d %s", r, (LPSTR)summ(buf, n + 12));
        fill(buf, 0xAA, (unsigned)n + 16);
        r = GetFileVersionInfo(p, h + 2, n, buf);
        lg("  GFVI(h+2, n) = %d %s", r, (LPSTR)summ(buf, n));
        fill(buf, 0xAA, 16);
        r = GetFileVersionInfo(p, h, 0, buf);
        lg("  GFVI(h, 0) = %d %s", r, (LPSTR)summ(buf, 4));
        fill(buf, 0xAA, 16);
        r = GetFileVersionInfo(p, h, 3, buf);
        lg("  GFVI(h, 3) = %d %s", r, (LPSTR)summ(buf, 6));
    } else {
        fill(buf, 0xAA, 1000);
        r = GetFileVersionInfo(p, 0, 1000, buf);
        lg("  GFVI(0, 1000) = %d %s", r, (LPSTR)summ(buf, 1000));
    }
}

static void t_size(void)
{
    char list[2000], p[100], *q, *e;
    HFILE f;
    int n;
    lg("== size");
    f = _lopen("C:\\VT\\LIST.TXT", OF_READ);
    n = _lread(f, list, sizeof list - 1);
    _lclose(f);
    list[n < 0 ? 0 : n] = 0;
    for (q = list; *q; q = e) {
        for (e = q; *e && *e != '\r'; e++)
            ;
        if (*e) {
            *e++ = 0;
            if (*e == '\n')
                e++;
        }
        wsprintf(p, "C:\\%s", (LPSTR)q);
        size1(p, q);
    }
    size1("C:\\VT\\MISSING.DLL", "MISSING.DLL");
    size1("C:\\VT\\BIN", "C:\\VT\\BIN (a directory)");
    size1("GOOD.DLL", "GOOD.DLL (no path, not on the search path)");
    size1("VFSYS.DLL", "VFSYS.DLL (no path, in SYSTEM)");
    size1("vfwin.dll", "vfwin.dll (no path, in WINDOWS, lower case)");
    size1("c:\\vt\\good.dll", "c:\\vt\\good.dll");
    size1("C:\\VT\\..\\VT\\GOOD.DLL", "C:\\VT\\..\\VT\\GOOD.DLL");
    size1("", "(empty name)");
    size1("C:\\VT\\SRC\\C400.DLL", "C:\\VT\\SRC\\C400.DLL (only C400.DL_ there)");
    {
        BOOL r;
        fill(buf, 0xAA, 64);
        r = GetFileVersionInfo("C:\\VT\\PLAIN.TXT", 10, 20, buf);
        lg(" GFVI(PLAIN.TXT, 10, 20) = %d %s", r, (LPSTR)summ(buf, 24));
        fill(buf, 0xAA, 64);
        r = GetFileResource("C:\\VT\\PLAIN.TXT", MAKEINTRESOURCE(16), MAKEINTRESOURCE(1), 10, 20,
                            buf);
        lg(" GetFileResource(PLAIN.TXT, 16, 1, 10, 20) = %d %s", r, (LPSTR)summ(buf, 24));
        fill(buf, 0xAA, 64);
        r = GetFileResource("C:\\VT\\GOOD.DLL", MAKEINTRESOURCE(10), MAKEINTRESOURCE(7), 0x100,
                            20, buf);
        lg(" GetFileResource(GOOD.DLL, 10, 7, 100h, 20) = %d %s", r, (LPSTR)summ(buf, 24));
        fill(buf, 0xAA, 64);
        r = GetFileVersionInfo("C:\\VT\\MISSING.DLL", 10, 20, buf);
        lg(" GFVI(MISSING.DLL, 10, 20) = %d %s", r, (LPSTR)summ(buf, 24));
        fill(buf, 0xAA, 64);
        r = GetFileVersionInfo("C:\\VT\\GOOD.DL_", 0x100, 20, buf);
        lg(" GFVI(GOOD.DL_, 100h, 20) = %d %s", r, (LPSTR)summ(buf, 24));
        fill(buf, 0xAA, 64);
        r = GetFileVersionInfo("C:\\VT\\PLAIN.TXT", 0x1000, 20, buf);
        lg(" GFVI(PLAIN.TXT, 1000h, 20) = %d %s", r, (LPSTR)summ(buf, 24));
    }
    if (hasd)
        size1("D:\\VT\\SRC\\V300.DLL", "D:\\VT\\SRC\\V300.DLL");
}

/* ---- VerQueryValue ---- */
static const char *const paths[] = {
    "\\", "", "\\\\", "\\VarFileInfo\\Translation", "\\VarFileInfo",
    "\\VARFILEINFO\\TRANSLATION", "\\varfileinfo\\translation", "VarFileInfo\\Translation",
    "\\VarFileInfo\\Translation\\", "\\VarFileInfo\\\\Translation", "\\VarFileInfo\\Transl",
    "\\VarFileInfo\\TranslationX", "\\VarFileInfo\\Other", "\\StringFileInfo",
    "\\StringFileInfo\\", "\\StringFileInfo\\040904E4", "\\StringFileInfo\\040904e4",
    "\\StringFileInfo\\040904E4\\CompanyName", "\\StringFileInfo\\040904e4\\CompanyName",
    "\\StringFileInfo\\040904E4\\companyname", "\\STRINGFILEINFO\\040904E4\\COMPANYNAME",
    "\\StringFileInfo\\040904E4\\FileDescription", "\\StringFileInfo\\040904E4\\FileVersion",
    "\\StringFileInfo\\040904E4\\InternalName", "\\StringFileInfo\\040904E4\\LegalCopyright",
    "\\StringFileInfo\\040904E4\\OriginalFilename", "\\StringFileInfo\\040904E4\\ProductName",
    "\\StringFileInfo\\040904E4\\ProductVersion", "\\StringFileInfo\\040904E4\\Comments",
    "\\StringFileInfo\\040904E4\\Empty", "\\StringFileInfo\\040904E4\\X",
    "\\StringFileInfo\\040904E4\\Company", "\\StringFileInfo\\040904E4\\CompanyNameX",
    "\\StringFileInfo\\040904E4\\CompanyName\\", "\\StringFileInfo\\040904E4\\CompanyName\\x",
    "\\StringFileInfo\\040704E4\\CompanyName", "\\StringFileInfo\\040704E4\\FileDescription",
    "\\StringFileInfo\\040704E4\\FileVersion", "\\StringFileInfo\\040904E4\\A",
    "\\StringFileInfo\\040904E4\\AB", "\\StringFileInfo\\040904E4\\ABC",
    "\\StringFileInfo\\040904E4\\ABCD", "\\StringFileInfo\\040904E4\\ABCDE",
    "\\Extra", "\\Extra\\Child", "\\VS_VERSION_INFO", "\\VS_VERSION_INFO\\StringFileInfo",
    "/StringFileInfo/040904E4/CompanyName", "StringFileInfo\\040904E4\\CompanyName",
    "\\\\StringFileInfo\\040904E4\\CompanyName", "\\StringFileInfo\\\\040904E4\\CompanyName",
    " \\", "\\ ", "\\StringFileInfo\\040904E4 \\CompanyName", 0 };

static void query(const BYTE FAR *blk, const char *what)
{
    int i;
    BOOL r;
    void FAR *v;
    UINT len;
    long off;
    lg(" -- %s", (LPSTR)what);
    for (i = 0; paths[i]; i++) {
        v = (void FAR *)MAKELP(0x1234, 0x5678);
        len = 0xBEEF;
        r = VerQueryValue(blk, paths[i], &v, &len);
        if (v == (void FAR *)MAKELP(0x1234, 0x5678))
            off = -99999L;
        else if (v == NULL)
            off = -88888L;
        else if (SELECTOROF(v) == SELECTOROF(blk))
            off = (long)OFFSETOF(v) - (long)OFFSETOF(blk);
        else
            off = -77777L;
        if (off >= 0 && off < 4000 && len < 400)
            lg("  \"%s\" = %d off %ld len %u: %s", (LPSTR)paths[i], r, off, len,
               (LPSTR)shows((const BYTE FAR *)v, len > 64 ? 64 : len));
        else
            lg("  \"%s\" = %d off %ld len %u", (LPSTR)paths[i], r, off, len);
    }
}

static void qfile(const char *name)
{
    char p[100], w[120];
    DWORD h, n;
    BOOL r;
    wsprintf(p, "C:\\VT\\%s", (LPSTR)name);
    n = GetFileVersionInfoSize(p, &h);
    fill(buf, 0, 4096);
    r = GetFileVersionInfo(p, h, n ? n : 2000, buf);
    wsprintf(w, "%s: size %ld, GFVI %d", (LPSTR)name, n, r);
    query(buf, w);
}

static void qraw(const char *name)
{
    char p[100], w[120];
    HFILE f;
    int n;
    wsprintf(p, "C:\\VT\\BIN\\%s", (LPSTR)name);
    fill(buf2, 0, 4096);
    f = _lopen(p, OF_READ);
    n = _lread(f, buf2, 4000);
    _lclose(f);
    wsprintf(w, "raw %s (%d bytes, zeros after)", (LPSTR)name, n);
    query(buf2, w);
}

static void t_query(void)
{
    DWORD h, n;
    char p[100];
    lg("== query");
    qfile("GOOD.DLL");
    qfile("RICH.DLL");
    qfile("LOWER.DLL");
    qfile("ODD.DLL");
    qfile("TWO.DLL");
    qfile("OWFIT.DLL");
    qfile("OW4.DLL");
    qfile("TBLBIG.DLL");
    qfile("LONG4.DLL");
    qfile("SHORT4.DLL");
    qfile("SIG.DLL");
    qfile("STRUC.DLL");
    qfile("KEYCASE.DLL");
    qraw("GOOD.BIN");
    qraw("OW.BIN");
    qraw("NOFIXED.BIN");
    qraw("VLEN0.BIN");
    qraw("VLEN40.BIN");
    qraw("KEY.BIN");
    qraw("SIG.BIN");
    qraw("LEN72.BIN");
    qraw("SHORT100.BIN");
    qraw("LONG12.BIN");
    qraw("LEN0.BIN");
    qraw("LEN4.BIN");
    qraw("LEN20.BIN");
    qraw("LEN71.BIN");
    qraw("LEN5.BIN");
    qraw("LEN8.BIN");
    qraw("LEN19.BIN");
    qraw("CH17.BIN");
    qraw("CH16.BIN");
    qraw("CH15.BIN");
    qraw("CH12.BIN");
    qraw("KEYSHORT.BIN");
    qraw("KEYSH20.BIN");
    {
        /* the block at an odd address and at 2 mod 4 */
        HFILE f = _lopen("C:\\VT\\BIN\\GOOD.BIN", OF_READ);
        fill(buf2, 0, 4096);
        _lread(f, buf2 + 1, 4000);
        _lclose(f);
        query(buf2 + 1, "raw GOOD.BIN at offset 1");
        f = _lopen("C:\\VT\\BIN\\GOOD.BIN", OF_READ);
        fill(buf2, 0, 4096);
        _lread(f, buf2 + 2, 4000);
        _lclose(f);
        query(buf2 + 2, "raw GOOD.BIN at offset 2");
    }
    /* a block cut short by GetFileVersionInfo's length: the rest zeros */
    lstrcpy(p, "C:\\VT\\RICH.DLL");
    n = GetFileVersionInfoSize(p, &h);
    fill(buf, 0, 4096);
    GetFileVersionInfo(p, h, 200, buf);
    query(buf, "RICH.DLL read with 200 bytes");
    /* the same with a mark in the rest */
    fill(buf, 0x41, 4096);
    GetFileVersionInfo(p, h, 300, buf);
    query(buf, "RICH.DLL read with 300 bytes, 41h after");
    (void)n;
}

/* ---- VerLanguageName ---- */
static void t_lang(void)
{
    char unk[100];
    UINT r, id, cnt = 0;
    static const UINT ids[] = { 0x409, 0x0C0C, 0, 0x400, 0x1, 0xFFFF, 0x10409 & 0xFFFF };
    static const UINT cbs[] = { 0, 1, 2, 5, 12, 13, 14, 100 };
    int i;
    lg("== language");
    fill(unk, 0x55, sizeof unk);
    r = VerLanguageName(0x0001, unk, sizeof unk);
    lg(" 0001: %u \"%s\"", r, (LPSTR)unk);
    for (id = 0;; id++) {
        fill(buf, 0x55, 100);
        r = VerLanguageName(id, (LPSTR)buf, 100);
        if (lstrcmp((LPSTR)buf, unk) || r != (UINT)lstrlen(unk)) {
            lg(" %04X: %u \"%s\"", id, r, (LPSTR)shows(buf, lstrlen((LPSTR)buf)));
            cnt++;
        }
        if (id == 0xFFFF)
            break;
    }
    lg(" %u identifiers with a name of their own", cnt);
    for (i = 0; i < 7; i++) {
        int j;
        for (j = 0; j < 8; j++) {
            fill(buf, 0x55, 40);
            r = VerLanguageName(ids[i], (LPSTR)buf, cbs[j]);
            lg(" %04X cb %u: %u %s", ids[i], cbs[j], r, (LPSTR)shows(buf, 24));
        }
    }
}

/* ---- VerFindFile ---- */
static void ff(UINT fl, const char *file, const char *win, const char *app, UINT curlen,
               UINT dstlen)
{
    char cur[300], dst[300];
    UINT cl = curlen, dl = dstlen, r;
    fill(cur, 0x55, sizeof cur);
    fill(dst, 0x55, sizeof dst);
    r = VerFindFile(fl, file, win, app, cur, &cl, dst, &dl);
    lg(" VerFindFile(%X, %s, %s, %s, %u, %u) = %04X", fl, (LPSTR)file, (LPSTR)win,
       (LPSTR)app, curlen, dstlen, r);
    lg("  cur %u \"%s\"", cl, (LPSTR)shows((BYTE *)cur, curlen < 60 ? curlen + 2 : 60));
    lg("  dst %u \"%s\"", dl, (LPSTR)shows((BYTE *)dst, dstlen < 60 ? dstlen + 2 : 60));
}

static void t_find(void)
{
    static const char *const files[] = { "NOWHERE.DLL", "VFWIN.DLL", "VFSYS.DLL", "VFAPP.DLL",
                                         "VFPATH.DLL", "VFCWD.DLL", "VFEXE.DLL", "VFBOTH.DLL",
                                         "VFAPPW.DLL", "vfsys.dll", "VER.DLL", 0 };
    static const char *const apps[] = { "C:\\VT\\APP", "C:\\VT\\APP\\", "c:\\vt\\app",
                                        "C:\\WINDOWS\\SYSTEM", "C:\\WINDOWS", "c:\\windows\\system\\",
                                        "", "C:\\", "C:\\VT\\NOTTHERE", 0 };
    static const UINT lens[][2] = { { 0, 0 }, { 1, 1 }, { 5, 300 }, { 300, 5 },
                                    { 18, 18 }, { 19, 19 }, { 20, 20 }, { 11, 11 }, { 12, 12 } };
    int i, j;
    HINSTANCE h1, h2, h3;
    lg("== find");
    for (i = 0; files[i]; i++) {
        ff(0, files[i], "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
        ff(VFFF_ISSHAREDFILE, files[i], "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    }
    for (i = 0; apps[i]; i++) {
        ff(0, "VFSYS.DLL", "C:\\WINDOWS", apps[i], 300, 300);
        ff(0, "NOWHERE.DLL", "C:\\WINDOWS", apps[i], 300, 300);
        ff(VFFF_ISSHAREDFILE, "VFAPP.DLL", "C:\\WINDOWS", apps[i], 300, 300);
    }
    ff(0, "VFSYS.DLL", "X:\\NOPE", "C:\\VT\\APP", 300, 300);
    ff(VFFF_ISSHAREDFILE, "NOWHERE.DLL", "X:\\NOPE", "C:\\VT\\APP", 300, 300);
    ff(VFFF_ISSHAREDFILE, "NOWHERE.DLL", "", "C:\\VT\\APP", 300, 300);
    ff(2, "VFSYS.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    ff(0xFFFF, "VFSYS.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    ff(0, "C:\\VT\\APP\\VFAPP.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    ff(0, "SYSTEM\\VFSYS.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    ff(0, "", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    for (j = 0; j < 9; j++) {
        ff(0, "VFSYS.DLL", "C:\\WINDOWS", "C:\\VT\\APP", lens[j][0], lens[j][1]);
        ff(VFFF_ISSHAREDFILE, "NOWHERE.DLL", "C:\\WINDOWS", "C:\\VT\\APP", lens[j][0], lens[j][1]);
        ff(0, "NOWHERE.DLL", "C:\\WINDOWS", "C:\\VT\\APP", lens[j][0], lens[j][1]);
    }
    /* files in use: loaded modules */
    h1 = LoadLibrary("C:\\WINDOWS\\SYSTEM\\VUSE.DLL");
    h2 = LoadLibrary("C:\\WINDOWS\\SYSTEM\\VUSEX.DLL");
    h3 = LoadLibrary("C:\\VT\\OTHER\\VUSE2.DLL");
    lg(" loaded VUSE %s, VUSEX (module XUSE) %s, OTHER\\VUSE2 %s", (LPSTR)(h1 >= 32 ? "ok" : "no"),
       (LPSTR)(h2 >= 32 ? "ok" : "no"), (LPSTR)(h3 >= 32 ? "ok" : "no"));
    ff(0, "VUSE.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    ff(VFFF_ISSHAREDFILE, "VUSE.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    ff(0, "VUSEX.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    ff(0, "VUSE2.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    ff(0, "XUSE.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    ff(0, "VUSE.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 0, 0);
    if (h1 >= 32)
        FreeLibrary(h1);
    if (h2 >= 32)
        FreeLibrary(h2);
    if (h3 >= 32)
        FreeLibrary(h3);
    ff(0, "VUSE.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    {
        HFILE f = _lopen("C:\\WINDOWS\\SYSTEM\\VUSE.DLL", OF_READ);
        lg(" VUSE.DLL open with _lopen");
        ff(0, "VUSE.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
        _lclose(f);
    }
    /* the current directory */
    dos_chdir("C:\\VT\\CWD");
    lg(" current directory C:\\VT\\CWD");
    ff(0, "VFCWD.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    ff(0, "VFSYS.DLL", "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
    ff(0, "NOWHERE.DLL", "C:\\WINDOWS", "", 300, 300);
    ff(0, "NOWHERE.DLL", "C:\\WINDOWS", "APP", 300, 300);
    {
        static const char *const f2[] = { "VFBOTH.DLL", "VFAPPW.DLL", "VFAPPP.DLL", "VFAPPS.DLL",
                                          "VFAPPC.DLL", "VFCWDS.DLL", "VFCWDW.DLL", "VFSYSP.DLL",
                                          "VFEXEW.DLL", "VFWIN.DLL", "VFSYS.DLL", 0 };
        for (i = 0; f2[i]; i++) {
            ff(0, f2[i], "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
            ff(VFFF_ISSHAREDFILE, f2[i], "C:\\WINDOWS", "C:\\VT\\APP", 300, 300);
            ff(VFFF_ISSHAREDFILE, f2[i], "C:\\WINDOWS", "C:\\VT\\OTHER", 300, 300);
        }
    }
    dos_chdir("C:\\VT");
    lg(" current directory C:\\VT");
    ff(0, "NOWHERE.DLL", "C:\\WINDOWS", "APP", 300, 300);
    ff(0, "VFAPP.DLL", "C:\\WINDOWS", "APP", 300, 300);
    ff(0, "VFAPP.DLL", "C:\\WINDOWS", "APP\\", 300, 300);
    ff(0, "VFSYS.DLL", "C:\\WINDOWS", "C:", 300, 300);
    ff(0, "VFSYS.DLL", "C:\\WINDOWS", "\\", 300, 300);
    ff(0, "VFSYS.DLL", "C:\\WINDOWS", "\\VT\\APP", 300, 300);
    ff(VFFF_ISSHAREDFILE, "VFAPP.DLL", "C:\\WINDOWS", "APP", 300, 300);
    dos_chdir("C:\\WINDOWS");
    if (hasd) {
        ff(0, "V300.DLL", "C:\\WINDOWS", "D:\\VT\\SRC", 300, 300);
        ff(0, "VFSYS.DLL", "C:\\WINDOWS", "D:\\VT\\DST", 300, 300);
    }
}

/* ---- exploration (not compared): KERNEL's GetModuleHandle with paths ---- */
static void t_kernel(void)
{
    static const char *const n[] = { "C:\\WINDOWS\\SYSTEM\\VUSEX.DLL", "VUSEX.DLL", "VUSEX",
                                     "XUSE", "XUSE.DLL", "C:\\WINDOWS\\SYSTEM\\XUSE.DLL",
                                     "C:\\VT\\VUSEX.DLL", "c:\\windows\\system\\vusex.dll", 0 };
    HINSTANCE h = LoadLibrary("C:\\WINDOWS\\SYSTEM\\VUSEX.DLL");
    HMODULE x = GetModuleHandle("XUSE");
    int i;
    lg("== kernel");
    for (i = 0; n[i]; i++) {
        HMODULE m = GetModuleHandle(n[i]);
        lg(" GetModuleHandle(%s) %s", (LPSTR)n[i], (LPSTR)(!m ? "0" : m == x ? "XUSE" : "other"));
    }
    FreeLibrary(h);
}

/* ---- VerInstallFile ---- */
static int ncase;

static void lists(void)
{
    listdir("C:\\VT\\DST");
    listdir("C:\\VT\\CUR");
    if (hasd) {
        listdir("D:\\VT\\DST");
        listdir("D:\\VT\\CUR");
    }
}

static void clean(void)
{
    cleandir("C:\\VT\\DST");
    cleandir("C:\\VT\\CUR");
    if (hasd) {
        cleandir("D:\\VT\\DST");
        cleandir("D:\\VT\\CUR");
    }
}

/* returns the result; tmp gets the temporary name */
static DWORD vi(UINT fl, const char *src, const char *dst, const char *sd, const char *dd,
                const char *cd, char *tmp, UINT tl)
{
    UINT len = tl;
    DWORD r;
    char ts[300];
    lstrcpy(ts, shows((BYTE *)tmp, lstrlen(tmp)));
    r = VerInstallFile(fl, src, dst, sd, dd, cd, tmp, &len);
    lg(" #%d VerInstallFile(%X, %s, %s, %s, %s, %s, \"%s\" %u) = %08lX tmp \"%s\" len %u",
       ++ncase, fl, (LPSTR)src, (LPSTR)dst, (LPSTR)sd, (LPSTR)dd, (LPSTR)(cd ? cd : "NULL"),
       (LPSTR)ts, tl, r, (LPSTR)shows((BYTE *)tmp, tl < 20 ? tl + 1 : 20), len);
    lists();
    return r;
}

/* a case: setup in DST (dstpool, as dname) and CUR (curpool), then install */
static DWORD icase(UINT fl, const char *src, const char *dname, const char *dstpool,
                   const char *curpool, char *tmp)
{
    clean();
    if (dstpool)
        place(dstpool, "C:\\VT\\DST", dname);
    if (curpool)
        place(curpool, "C:\\VT\\CUR", dname);
    tmp[0] = 0;
    return vi(fl, src, dname, "C:\\VT\\SRC", "C:\\VT\\DST",
              curpool ? "C:\\VT\\CUR" : "C:\\VT\\DST", tmp, 144);
}

static void t_inst(void)
{
    char tmp[300], t2[300];
    DWORD r;
    UINT k;
    HINSTANCE h;
    static const char *const vs[] = { "V300.DLL", "V310.DLL", "V310B.DLL", "V400.DLL",
                                      "VPROD.DLL", "VFILE.DLL", "VFLO.DLL", "VGER.DLL",
                                      "VCP.DLL", "VCP1250.DLL", "VNEU.DLL", "VLANG0.DLL",
                                      "VCP0.DLL", "VGER437.DLL", "VMULTI.DLL", "VMULTI2.DLL",
                                      "VTYPE.DLL", "VSUB.DLL", "VOS.DLL", "V200GER.DLL",
                                      "V400GER.DLL", "V400TYP.DLL", "VNOTR.DLL", "VNONE.DLL",
                                      "VBAD.DLL", 0 };
    int i;
    fill(tmp, 0x55, sizeof tmp);
    tmp[0] = 0;
    lg("== install");
    /* nothing there yet */
    icase(0, "V310.DLL", "T.DLL", 0, 0, tmp);
    /* every source over V310 and V310 over every destination */
    for (i = 0; vs[i]; i++)
        icase(0, vs[i], "T.DLL", "V310.DLL", 0, tmp);
    for (i = 0; vs[i]; i++)
        icase(0, "V310.DLL", "T.DLL", vs[i], 0, tmp);
    icase(0, "V400GER.DLL", "T.DLL", "V310.DLL", 0, tmp);
    icase(0, "V200GER.DLL", "T.DLL", "V310.DLL", 0, tmp);
    icase(0, "V400TYP.DLL", "T.DLL", "V310.DLL", 0, tmp);
    icase(0, "VMULTI.DLL", "T.DLL", "VGER.DLL", 0, tmp);
    icase(0, "VGER.DLL", "T.DLL", "VMULTI2.DLL", 0, tmp);
    icase(0, "TEXT.TXT", "T.TXT", "TEXT2.TXT", 0, tmp);
    icase(0, "TEXT.TXT", "T.TXT", 0, 0, tmp);
    /* forced */
    icase(VIFF_FORCEINSTALL, "V300.DLL", "T.DLL", "V310.DLL", 0, tmp);
    icase(VIFF_FORCEINSTALL, "VGER.DLL", "T.DLL", "V310.DLL", 0, tmp);
    icase(VIFF_FORCEINSTALL | VIFF_DONTDELETEOLD, "V300.DLL", "T.DLL", "V310.DLL", 0, tmp);
    /* the second call: the temporary file, forced */
    r = icase(0, "V300.DLL", "T.DLL", "V310.DLL", 0, tmp);
    if (r & VIF_TEMPFILE) {
        lstrcpy(t2, tmp);
        vi(VIFF_FORCEINSTALL, t2, "T.DLL", "C:\\VT\\DST", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    }
    r = icase(0, "V300.DLL", "T.DLL", "V310.DLL", 0, tmp);
    if (r & VIF_TEMPFILE) {
        lstrcpy(t2, tmp);
        tmp[0] = 0;
        vi(VIFF_FORCEINSTALL, t2, "T.DLL", "C:\\VT\\DST", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    }
    r = icase(0, "V300.DLL", "T.DLL", "V310.DLL", 0, tmp);
    if (r & VIF_TEMPFILE) {
        lstrcpy(t2, tmp);
        vi(VIFF_FORCEINSTALL, t2, "T.DLL", "C:\\VT\\DST", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 0);
    }
    r = icase(0, "V300.DLL", "T.DLL", "V310.DLL", 0, tmp);
    if (r & VIF_TEMPFILE) {
        lstrcpy(t2, tmp);
        vi(0, t2, "T.DLL", "C:\\VT\\DST", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    }
    r = icase(0, "V300.DLL", "T.DLL", "V310.DLL", 0, tmp);
    if (r & VIF_TEMPFILE) {
        /* forced with the source again, the temporary name in the buffer */
        vi(VIFF_FORCEINSTALL, "V300.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST",
           tmp, 144);
    }
    /* the temporary name's buffer */
    for (k = 0; k < 16; k++) {
        clean();
        place("V310.DLL", "C:\\VT\\DST", "T.DLL");
        fill(tmp, 0x55, 40);
        tmp[0] = 0;
        vi(0, "V300.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, k);
    }
    clean();
    fill(tmp, 0x55, 40);
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 0);
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    lstrcpy(tmp, "JUNK.TMP");
    vi(0, "V300.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    /* the current version elsewhere */
    icase(0, "V400.DLL", "T.DLL", 0, "V310.DLL", tmp);
    icase(0, "V300.DLL", "T.DLL", 0, "V310.DLL", tmp);
    icase(0, "VGER.DLL", "T.DLL", 0, "V310.DLL", tmp);
    icase(VIFF_DONTDELETEOLD, "V400.DLL", "T.DLL", 0, "V310.DLL", tmp);
    icase(VIFF_FORCEINSTALL, "V300.DLL", "T.DLL", 0, "V310.DLL", tmp);
    icase(VIFF_FORCEINSTALL | VIFF_DONTDELETEOLD, "V300.DLL", "T.DLL", 0, "V310.DLL", tmp);
    icase(0, "V400.DLL", "T.DLL", "V300.DLL", "V310.DLL", tmp);
    icase(0, "V400.DLL", "T.DLL", "V310.DLL", "V300.DLL", tmp);
    icase(0, "V300.DLL", "T.DLL", "V400.DLL", "V200GER.DLL", tmp);
    icase(VIFF_DONTDELETEOLD, "V400.DLL", "T.DLL", "V300.DLL", "V310.DLL", tmp);
    /* the current directory as NULL and "" */
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", NULL, tmp, 144);
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    vi(0, "V300.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", NULL, tmp, 144);
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    tmp[0] = 0;
    vi(0, "V300.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "", tmp, 144);
    clean();
    place("V310.DLL", "C:\\VT\\CUR", "T.DLL");
    tmp[0] = 0;
    vi(0, "V300.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "", tmp, 144);
    /* read-only */
    clean();
    place("V300.DLL", "C:\\VT\\DST", "T.DLL");
    dos_setattr("C:\\VT\\DST\\T.DLL", 1);
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    tmp[0] = 0;
    vi(VIFF_FORCEINSTALL, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp,
       144);
    clean();
    place("V300.DLL", "C:\\VT\\CUR", "T.DLL");
    dos_setattr("C:\\VT\\CUR\\T.DLL", 1);
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\CUR", tmp, 144);
    clean();
    place("V300.DLL", "C:\\VT\\DST", "T.DLL");
    dos_setattr("C:\\VT\\DST\\T.DLL", 2);
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    clean();
    place("V300.DLL", "C:\\VT\\DST", "T.DLL");
    dos_setattr("C:\\VT\\DST\\T.DLL", 4);
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    /* in use: a loaded module */
    clean();
    place("VUSE.DLL", "C:\\VT\\DST", "VUSE.DLL");
    h = LoadLibrary("C:\\VT\\DST\\VUSE.DLL");
    lg(" loaded DST\\VUSE.DLL: %s", (LPSTR)(h >= 32 ? "ok" : "no"));
    tmp[0] = 0;
    vi(0, "V400.DLL", "VUSE.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    tmp[0] = 0;
    vi(VIFF_FORCEINSTALL, "V400.DLL", "VUSE.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST",
       tmp, 144);
    tmp[0] = 0;
    vi(0, "V300.DLL", "VUSE.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    if (h >= 32)
        FreeLibrary(h);
    clean();
    place("VUSE.DLL", "C:\\VT\\CUR", "VUSE.DLL");
    h = LoadLibrary("C:\\VT\\CUR\\VUSE.DLL");
    tmp[0] = 0;
    vi(0, "V400.DLL", "VUSE.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\CUR", tmp, 144);
    if (h >= 32)
        FreeLibrary(h);
    clean();
    place("VUSEX.DLL", "C:\\VT\\DST", "VUSEX.DLL");
    h = LoadLibrary("C:\\VT\\DST\\VUSEX.DLL");
    tmp[0] = 0;
    vi(0, "V400.DLL", "VUSEX.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    if (h >= 32)
        FreeLibrary(h);
    /* in use: open */
    clean();
    place("V300.DLL", "C:\\VT\\DST", "T.DLL");
    {
        HFILE f = _lopen("C:\\VT\\DST\\T.DLL", OF_READ);
        lg(" DST\\T.DLL open with _lopen");
        tmp[0] = 0;
        vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
        _lclose(f);
    }
    /* sources: missing, compressed, odd names and directories */
    icase(0, "MISSING.DLL", "T.DLL", "V310.DLL", 0, tmp);
    icase(0, "MISSING.DLL", "T.DLL", 0, 0, tmp);
    icase(VIFF_FORCEINSTALL, "MISSING.DLL", "T.DLL", "V310.DLL", 0, tmp);
    icase(0, "C400.DL_", "T.DLL", "V310.DLL", 0, tmp);
    icase(0, "C300.DL_", "T.DLL", "V310.DLL", 0, tmp);
    icase(0, "C400.DLL", "T.DLL", "V310.DLL", 0, tmp);
    icase(0, "R400.DLL", "T.DLL", "V310.DLL", 0, tmp);
    icase(0, "CTRUNC.DL_", "T.DLL", "V310.DLL", 0, tmp);
    icase(0, "CTRUNC.DL_", "T.DLL", 0, 0, tmp);
    icase(0, "CBADALG.DL_", "T.DLL", "V310.DLL", 0, tmp);
    icase(0, "TEXT2.TX_", "T.TXT", 0, 0, tmp);
    icase(0, "TEXT2.TXT", "T.TXT", "TEXT.TXT", 0, tmp);
    icase(0, "BIG.DAT", "BIG.DAT", 0, 0, tmp);
    icase(0, "BIG.DA_", "BIG.DAT", 0, 0, tmp);
    icase(0, "lower.dll", "t.dll", "V310.DLL", 0, tmp);
    icase(0, "V400.DLL", "NEWNAME.DLL", 0, 0, tmp);
    icase(0, "SRC\\V400.DLL", "T.DLL", 0, 0, tmp);
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC\\", "C:\\VT\\DST\\", "C:\\VT\\DST\\", tmp, 144);
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    tmp[0] = 0;
    vi(0, "V300.DLL", "T.DLL", "c:\\vt\\src", "c:\\vt\\dst", "c:\\vt\\dst", tmp, 144);
    clean();
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\NOTTHERE", "C:\\VT\\NOTTHERE", tmp, 144);
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\NOSRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    clean();
    dos_chdir("C:\\VT\\SRC");
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    dos_chdir("C:\\VT");
    tmp[0] = 0;
    vi(0, "V300.DLL", "T.DLL", "SRC", "DST", "DST", tmp, 144);
    dos_chdir("C:\\WINDOWS");
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST\\T.DLL", tmp, 144);
    /* a directory where the destination file would be */
    clean();
    dos_mkdir("C:\\VT\\DST\\T.DLL");
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    dos_rmdir("C:\\VT\\DST\\T.DLL");
    /* read-only: the attribute as set first */
    clean();
    place("V400.DLL", "C:\\VT\\DST", "T.DLL");
    i = dos_setattr("C:\\VT\\DST\\T.DLL", 1);
    lg(" setattr 1: %d, attr now %02X", i, dos_getattr("C:\\VT\\DST\\T.DLL"));
    place("V400.DLL", "C:\\VT\\CUR", "RO.DLL");
    dos_setattr("C:\\VT\\CUR\\RO.DLL", 1);
    lg(" deleting a read-only file: %d", dos_unlink("C:\\VT\\CUR\\RO.DLL"));
    dos_setattr("C:\\VT\\CUR\\RO.DLL", 0);
    dos_unlink("C:\\VT\\CUR\\RO.DLL");
    tmp[0] = 0;
    vi(0, "V300.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    tmp[0] = 0;
    vi(0, "V310.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    /* in use, and older or another language */
    clean();
    place("VUSE.DLL", "C:\\VT\\DST", "VUSE.DLL");
    h = LoadLibrary("C:\\VT\\DST\\VUSE.DLL");
    tmp[0] = 0;
    vi(0, "V200GER.DLL", "VUSE.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    tmp[0] = 0;
    vi(0, "VTYPE.DLL", "VUSE.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    tmp[0] = 0;
    vi(0, "V200GER.DLL", "VUSE.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 3);
    tmp[0] = 0;
    vi(0, "MISSING.DLL", "VUSE.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    place("V300.DLL", "C:\\VT\\DST", "temp.000");
    lstrcpy(tmp, "temp.000");
    vi(VIFF_FORCEINSTALL, "V400.DLL", "VUSE.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST",
       tmp, 144);
    if (h >= 32)
        FreeLibrary(h);
    /* temporary names already taken */
    clean();
    place("V400.DLL", "C:\\VT\\DST", "T.DLL");
    for (k = 0; k <= 10; k++) {
        wsprintf(t2, "TEMP.%03u", k);
        place("TEXT.TXT", "C:\\VT\\DST", t2);
    }
    place("TEXT.TXT", "C:\\VT\\DST", "TEMP.012");
    tmp[0] = 0;
    vi(0, "V300.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    tmp[0] = 0;
    vi(0, "V300.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    tmp[0] = 0;
    vi(0, "V300.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    /* the current directory spelt differently from the destination */
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "c:\\vt\\dst\\", tmp, 144);
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST\\", "C:\\VT\\DST", tmp, 144);
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    dos_chdir("C:\\VT");
    tmp[0] = 0;
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "DST", tmp, 144);
    dos_chdir("C:\\WINDOWS");
    /* forced with a temporary name that is not there, or another name */
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    lstrcpy(tmp, "NOPE.TMP");
    vi(VIFF_FORCEINSTALL, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp,
       144);
    clean();
    place("V310.DLL", "C:\\VT\\CUR", "T.DLL");
    place("V300.DLL", "C:\\VT\\DST", "X.TMP");
    lstrcpy(tmp, "X.TMP");
    vi(VIFF_FORCEINSTALL, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\CUR", tmp,
       144);
    clean();
    place("V310.DLL", "C:\\VT\\CUR", "T.DLL");
    place("V300.DLL", "C:\\VT\\DST", "X.TMP");
    lstrcpy(tmp, "X.TMP");
    vi(VIFF_FORCEINSTALL | VIFF_DONTDELETEOLD, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST",
       "C:\\VT\\CUR", tmp, 144);
    clean();
    place("V300.DLL", "C:\\VT\\DST", "X.TMP");
    lstrcpy(tmp, "X.TMP");
    vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    /* the source is the destination */
    clean();
    place("V310.DLL", "C:\\VT\\DST", "T.DLL");
    tmp[0] = 0;
    vi(0, "T.DLL", "T.DLL", "C:\\VT\\DST", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
    /* code pages, and languages with nothing installed */
    {
        static const char *const cps[] = { "VCP932.DLL", "VCP1200.DLL", "VCP437.DLL", "VCPFFFF.DLL",
                                           "VUS932.DLL", "VGER.DLL", "VCP.DLL", "VNEU.DLL", 0 };
        for (i = 0; cps[i]; i++) {
            icase(0, cps[i], "T.DLL", 0, 0, tmp);
            icase(0, cps[i], "T.DLL", "V310.DLL", 0, tmp);
            icase(0, "V310.DLL", "T.DLL", cps[i], 0, tmp);
        }
        icase(0, "VCP932.DLL", "T.DLL", "VUS932.DLL", 0, tmp);
        icase(0, "VUS932.DLL", "T.DLL", "VCP932.DLL", 0, tmp);
    }
    /* cross-drive */
    if (hasd) {
        clean();
        place("V310.DLL", "C:\\VT\\DST", "T.DLL");
        tmp[0] = 0;
        vi(0, "V400.DLL", "T.DLL", "D:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
        clean();
        place("V310.DLL", "C:\\VT\\DST", "T.DLL");
        tmp[0] = 0;
        vi(0, "C400.DL_", "T.DLL", "D:\\VT\\SRC", "C:\\VT\\DST", "C:\\VT\\DST", tmp, 144);
        clean();
        place("V310.DLL", "D:\\VT\\CUR", "T.DLL");
        tmp[0] = 0;
        vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "C:\\VT\\DST", "D:\\VT\\CUR", tmp, 144);
        clean();
        place("V310.DLL", "C:\\VT\\CUR", "T.DLL");
        tmp[0] = 0;
        vi(0, "V400.DLL", "T.DLL", "C:\\VT\\SRC", "D:\\VT\\DST", "C:\\VT\\CUR", tmp, 144);
        clean();
        place("V310.DLL", "C:\\VT\\CUR", "T.DLL");
        tmp[0] = 0;
        vi(0, "V300.DLL", "T.DLL", "C:\\VT\\SRC", "D:\\VT\\DST", "C:\\VT\\CUR", tmp, 144);
        if (tmp[0]) {
            lstrcpy(t2, tmp);
            vi(VIFF_FORCEINSTALL, t2, "T.DLL", "D:\\VT\\DST", "D:\\VT\\DST", "C:\\VT\\CUR", tmp,
               144);
        }
    }
    clean();
}

/* ---- the versions of the files named in C:\VT\MODS.TXT ---- */
static void t_mods(void)
{
    char list[3000], *q, *e, w[200];
    HFILE f;
    int n;
    DWORD h, sz;
    UINT len, i;
    void FAR *v;
    VS_FIXEDFILEINFO FAR *ff;
    static const char *const keys[] = { "CompanyName", "FileDescription", "FileVersion",
                                        "InternalName", "LegalCopyright", "OriginalFilename",
                                        "ProductName", "ProductVersion", 0 };
    f = _lopen("C:\\VT\\MODS.TXT", OF_READ);
    if (f == HFILE_ERROR)
        return;
    lg("== modules");
    n = _lread(f, list, sizeof list - 1);
    _lclose(f);
    list[n < 0 ? 0 : n] = 0;
    for (q = list; *q; q = e) {
        for (e = q; *e && *e != '\r' && *e != '\n'; e++)
            ;
        while (*e == '\r' || *e == '\n')
            *e++ = 0;
        sz = GetFileVersionInfoSize(q, &h);
        lg(" %s: size %ld", (LPSTR)q, sz);
        if (!sz)
            continue;
        fill(buf, 0, 4096);
        if (!GetFileVersionInfo(q, h, sz, buf)) {
            lg("  GetFileVersionInfo failed");
            continue;
        }
        if (VerQueryValue(buf, "\\", &v, &len) && len >= sizeof *ff) {
            ff = v;
            lg("  fixed: sig %08lX file %u.%u.%u.%u product %u.%u.%u.%u os %lX type %lX/%lX",
               ff->dwSignature, HIWORD(ff->dwFileVersionMS), LOWORD(ff->dwFileVersionMS),
               HIWORD(ff->dwFileVersionLS), LOWORD(ff->dwFileVersionLS),
               HIWORD(ff->dwProductVersionMS), LOWORD(ff->dwProductVersionMS),
               HIWORD(ff->dwProductVersionLS), LOWORD(ff->dwProductVersionLS),
               ff->dwFileOS, ff->dwFileType, ff->dwFileSubtype);
        } else
            lg("  no fixed part");
        if (VerQueryValue(buf, "\\VarFileInfo\\Translation", &v, &len) && len >= 4) {
            WORD FAR *tr = v;
            lg("  translation %04X %04X", tr[0], tr[1]);
            for (i = 0; keys[i]; i++) {
                char path[80];
                wsprintf(path, "\\StringFileInfo\\%04X%04X\\%s", tr[0], tr[1], (LPSTR)keys[i]);
                if (VerQueryValue(buf, path, &v, &len))
                    wsprintf(w, "\"%s\"", (LPSTR)v);
                else
                    lstrcpy(w, "missing");
                lg("  %s %s", (LPSTR)keys[i], (LPSTR)w);
            }
        } else
            lg("  no translation");
    }
}

int __cdecl __far tmain(void)
{
    static char arg[40] = "ersqlfim";
    HGLOBAL g1 = GlobalAlloc(GMEM_FIXED, 8192L), g2 = GlobalAlloc(GMEM_FIXED, 8192L);
    HFILE f;
    buf = (BYTE __far *)GlobalLock(g1);
    buf2 = (BYTE __far *)GlobalLock(g2);
    SetHandleCount(40);
    f = _lopen("C:\\VRP.ARG", OF_READ);
    if (f != HFILE_ERROR) {
        int n = _lread(f, arg, sizeof arg - 1);
        arg[n < 0 ? 0 : n] = 0;
        _lclose(f);
    }
    f = _lcreat(logname, 0);
    _lclose(f);
    hasd = dos_getattr("D:\\VT\\DST\\KEEP") >= 0;
    lg("VRP start; D: %s", (LPSTR)(hasd ? "yes" : "no"));
    fixdates("C:\\VT\\SRC");
    fixdates("C:\\VT\\POOL");
    if (hasd)
        fixdates("D:\\VT\\SRC");
    {
        const char *a;
        for (a = arg; *a; a++) {
            switch (*a) {
            case 'e': t_exports(); break;
            case 'r': t_res(); break;
            case 's': t_size(); break;
            case 'q': t_query(); break;
            case 'l': t_lang(); break;
            case 'f': t_find(); break;
            case 'i': t_inst(); break;
            case 'm': t_mods(); break;
            case 'k': t_kernel(); break;
            }
        }
    }
    lg("VRP end");
    return 0;
}
