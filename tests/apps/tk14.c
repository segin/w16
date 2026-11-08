/*
 * tk14.c - profiles (WIN.INI and private .INI files) and file I/O:
 * GetProfileInt/String, WriteProfileString, GetPrivateProfileInt/String,
 * WritePrivateProfileString; OpenFile, _lopen, _lcreat, _lclose, _lread,
 * _lwrite, _llseek, _hread, _hwrite, GetTempFileName, GetTempDrive,
 * GetDriveType, SetHandleCount.
 *
 * The probe makes its own files and directories with plain INT 21h
 * (C:\TKINI, C:\TKCWD, C:\TKPATH, C:\TKTMP, and copies of a search file in
 * the Windows and system directories), makes C:\TKCWD the current
 * directory, and reads the files back raw after each write, so a run
 * shows exactly what the APIs read and wrote. Expected layout (as
 * tests/tk14ref.sh and tests/run-tests.sh set it up): TK14.EXE in C:\,
 * the Windows directory C:\WINDOWS, the system directory C:\WINDOWS\SYSTEM,
 * PATH=C:\TKPATH;Z:\ and TEMP=C:\TKTMP. Only [TK14] of WIN.INI is used.
 *
 * Strings are printed as OBS name="text" (bytes outside 20h-7Eh as \xx)
 * followed by OBS name#=hash, which tools/obsref.py turns into a check.
 * Output and checks: see tklog.h; reference values in tk14ref.h.
 */
#include <windows.h>

#define TK_REF "tk14ref.h"
#include "tklog.h"

extern int __cdecl dos_int21(WORD FAR *r);
extern WORD __cdecl get_ahincr(void);
extern WORD __cdecl get_0040h(void);

/* ---- small helpers ---- */

static WORD slen(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    return n;
}

static void scpy(char FAR *d, const char FAR *s)
{
    while ((*d++ = *s++) != 0)
        ;
}

static void fill(char FAR *b, WORD n, char c)
{
    WORD i;
    for (i = 0; i < n; i++)
        b[i] = c;
}

static int same_n(const char FAR *a, const char FAR *b, WORD n)
{
    WORD i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static const char *nm(const char *a, const char *b)
{
    static char buf[100];
    WORD i = 0;
    while (*a && i < 60)
        buf[i++] = *a++;
    while (*b && i < 98)
        buf[i++] = *b++;
    buf[i] = 0;
    return buf;
}

static WORD hashn(const char FAR *s, WORD n)
{
    WORD h = n, i;
    for (i = 0; i < n; i++)
        h = (WORD)((h << 5) | (h >> 11)) ^ (BYTE)s[i];
    return h;
}

/* n bytes, escaped, then name#=hash */
static void obs_b(const char *name, const char FAR *s, WORD n)
{
    char e[4];
    char hn[100];
    WORD i;
    out("OBS ");
    out(name);
    out("=\"");
    for (i = 0; i < n; i++) {
        BYTE c = (BYTE)s[i];
        if (c >= 0x20 && c < 0x7F && c != '\\' && c != '"') {
            e[0] = (char)c;
            e[1] = 0;
        } else {
            e[0] = '\\';
            e[1] = "0123456789ABCDEF"[c >> 4];
            e[2] = "0123456789ABCDEF"[c & 15];
            e[3] = 0;
        }
        out(e);
    }
    out("\"\r\n");
    if (name[0] == '~')         /* not checked */
        return;
    scpy(hn, name);
    scpy(hn + slen(hn), "#");
    obs(hn, hashn(s, n));
}

static void obs_s(const char *name, const char FAR *s)
{
    obs_b(name, s, slen(s));
}

/* ---- DOS ---- */

static int dos(WORD ax, WORD bx, WORD cx, WORD dx, WORD ds_, WORD *axo, WORD *cxo, WORD *dxo)
{
    WORD r[8];
    int c;
    r[0] = ax; r[1] = bx; r[2] = cx; r[3] = dx;
    r[4] = r[5] = 0;
    r[6] = ds_;
    r[7] = 0;
    c = dos_int21(r);
    if (axo) *axo = r[0];
    if (cxo) *cxo = r[2];
    if (dxo) *dxo = r[3];
    return !c;
}

static int dos_p(WORD ax, WORD cx, const char FAR *p, WORD *axo)
{
    return dos(ax, 0, cx, OFFSETOF(p), SELECTOROF(p), axo, 0, 0);
}

static void d_mkdir(const char *p) { dos_p(0x3900, 0, p, 0); }
static void d_chdir(const char *p) { dos_p(0x3B00, 0, p, 0); }
static void d_del(const char *p) { dos_p(0x4100, 0, p, 0); }
static void d_setattr(const char *p, WORD a) { dos_p(0x4301, a, p, 0); }

static int d_exists(const char *p)
{
    WORD ax;
    return dos_p(0x4300, 0, p, &ax);
}

static void d_write(const char *p, const char FAR *data, WORD n)
{
    WORD h;
    if (!dos_p(0x3C00, 0, p, &h))
        return;
    dos(0x4000, h, n, OFFSETOF(data), SELECTOROF(data), 0, 0, 0);
    dos(0x3E00, h, 0, 0, 0, 0, 0, 0);
}

static void d_puts(const char *p, const char *data)
{
    d_write(p, data, slen(data));
}

/* read a file raw; returns its length, FFFFh if it does not exist */
static WORD d_read(const char *p, char FAR *buf, WORD max)
{
    WORD h, n;
    if (!dos_p(0x3D00, 0, p, &h))
        return 0xFFFF;
    if (!dos(0x3F00, h, max, OFFSETOF(buf), SELECTOROF(buf), &n, 0, 0))
        n = 0;
    dos(0x3E00, h, 0, 0, 0, 0, 0, 0);
    return n;
}

static WORD d_size(const char *p)
{
    static char b[4096];
    return d_read(p, b, sizeof b);
}

/* date and time of a file (INT 21h 5700h) */
static int d_stamp(const char *p, WORD *date, WORD *time)
{
    WORD h;
    if (!dos_p(0x3D00, 0, p, &h))
        return 0;
    dos(0x5700, h, 0, 0, 0, 0, time, date);
    dos(0x3E00, h, 0, 0, 0, 0, 0, 0);
    return 1;
}

static void d_setstamp(const char *p, WORD date, WORD time)
{
    WORD h;
    if (!dos_p(0x3D02, 0, p, &h))
        return;
    dos(0x5701, h, time, date, 0, 0, 0, 0);
    dos(0x3E00, h, 0, 0, 0, 0, 0, 0);
}

static void dumpfile(const char *name, const char *p)
{
    static char b[2048];
    WORD n = d_read(p, b, sizeof b);
    if (n == 0xFFFF) {
        obs(nm(name, " missing"), 1);
        return;
    }
    obs_b(name, b, n);
}

/* ---- profiles: reading ---- */

static const char ini[] =
    "; leading comment\r\n"
    "\r\n"
    "[Sect1]\r\n"
    "key1=value1\r\n"
    "  key2  =  value2  \r\n"
    "KEY3=value3 ; comment?\r\n"
    "key4=\"quoted value\"\r\n"
    "key5=' single '\r\n"
    "key6=\r\n"
    "key7\r\n"
    "=noname\r\n"
    "key8=a=b\r\n"
    "dup=first\r\n"
    "dup=second\r\n"
    ";key9=commented\r\n"
    "tab\t=\tv\t\r\n"
    "key10 = \"  q  \" \r\n"
    "Key 11=sp\r\n"
    "\r\n"
    " [Sect2] \r\n"
    "k=v\r\n"
    "[sect1]\r\n"
    "key1=again\r\n"
    "only2=x\r\n"
    "[Sect3\r\n"
    "k3=v3\r\n"
    "[ Spaced ]\r\n"
    "s=1\r\n"
    "[]\r\n"
    "e=1\r\n"
    "[Br]x]\r\n"
    "b=1\r\n"
    "[Num]\r\n"
    "n1=123\r\nn2=-5\r\nn3=0x1F\r\nn4=  42abc\r\nn5=abc\r\nn6=65536\r\n"
    "n7=99999\r\nn8=+7\r\nn9=\r\nn10=  -  3\r\nn11=-0\r\nn12=4294967297\r\n"
    "n13=1 2\r\nn14=\t9\r\nn15=-32768\r\nn16=32768\r\nn17=-65535\r\n"
    "n18=0xff\r\nn19=0XAB\r\nn20=-0x10\r\nn21=x5\r\nn22=012\r\nn23=08\r\n"
    "n24=0x\r\nn25=0xg\r\nn26=  -12\r\nn27=0x1F2E3D\r\nn28=\"5\"\r\nn29=0x-5\r\n"
    "[LF]\n"
    "lf1=unix\n"
    "lf2=x\r\n"
    "[CR]\r"
    "cr1=mac\r"
    "cr2=y\r\n"
    "[Last]\r\n"
    "tail=x";

#define INI "C:\\TKINI\\TK.INI"

static char buf[2048];

static void gps(const char *tag_, const char *sec, const char *key,
                const char *def, int n, const char *file)
{
    char tag[100];
    int r;
    scpy(tag, tag_);
    fill(buf, sizeof buf, '~');
    r = GetPrivateProfileString(sec, key, def, buf, n, file);
    if (tag[0] == '~')
        obs_env(nm(tag + 1, " ret"), (WORD)r);
    else
        obs(nm(tag, " ret"), (WORD)r);
    obs_b(tag, buf, (WORD)r + 2 <= sizeof buf ? (WORD)r + 2 : sizeof buf);
}

static void probe_read(void)
{
    static const char *const keys[] = {
        "key1", "key2", "KEY3", "key4", "key5", "key6", "key7", "", "key8",
        "dup", ";key9", "tab", "key10", "Key 11", "key 11", " key1",
        "key1 ", "KEY1", "noname", 0
    };
    static char longval[1600];
    WORD i;

    d_write(INI, ini, sizeof ini - 1);
    for (i = 0; keys[i]; i++)
        gps(nm("Sect1/", keys[i]), "Sect1", keys[i], "DEF", 80, INI);
    gps("SECT1/key1", "SECT1", "key1", "DEF", 80, INI);
    gps(" Sect1/key1", " Sect1", "key1", "DEF", 80, INI);
    gps("Sect1 /key1", "Sect1 ", "key1", "DEF", 80, INI);
    gps("Sect2/k", "Sect2", "k", "DEF", 80, INI);
    gps("sect1/only2", "sect1", "only2", "DEF", 80, INI);
    gps("Sect3/k3", "Sect3", "k3", "DEF", 80, INI);
    gps("Spaced/s", "Spaced", "s", "DEF", 80, INI);
    gps(" Spaced /s", " Spaced ", "s", "DEF", 80, INI);
    gps("(empty)/e", "", "e", "DEF", 80, INI);
    gps("Br]x/b", "Br]x", "b", "DEF", 80, INI);
    gps("Br/b", "Br", "b", "DEF", 80, INI);
    gps("LF/lf1", "LF", "lf1", "DEF", 80, INI);
    gps("LF/lf2", "LF", "lf2", "DEF", 80, INI);
    gps("CR/cr1", "CR", "cr1", "DEF", 80, INI);
    gps("CR/cr2", "CR", "cr2", "DEF", 80, INI);
    gps("Last/tail", "Last", "tail", "DEF", 80, INI);
    gps("Num/n10", "Num", "n10", "DEF", 80, INI);

    /* defaults */
    gps("missing key", "Sect1", "nokey", "def", 80, INI);
    gps("missing section", "NoSect", "key1", "def", 80, INI);
    gps("missing file", "Sect1", "key1", "def", 80, "C:\\TKINI\\NONE.INI");
    gps("default spaces", "Sect1", "nokey", "  def  ", 80, INI);
    gps("default quoted", "Sect1", "nokey", "\"def\"", 80, INI);
    gps("default empty", "Sect1", "nokey", "", 80, INI);
    gps("default long n=4", "Sect1", "nokey", "default", 4, INI);

    /* buffer sizes */
    for (i = 0; i <= 7; i++) {
        char t[20];
        scpy(t, "key1 n=0");
        t[7] = (char)('0' + i);
        gps(t, "Sect1", "key1", "DEF", i, INI);
    }

    /* key lists (key NULL) */
    gps("keys Sect1 n=200", "Sect1", NULL, "DEF", 200, INI);
    gps("keys Sect1 n=12", "Sect1", NULL, "DEF", 12, INI);
    gps("keys Sect1 n=63", "Sect1", NULL, "DEF", 63, INI);
    gps("keys Sect1 n=62", "Sect1", NULL, "DEF", 62, INI);
    gps("keys Sect1 n=61", "Sect1", NULL, "DEF", 61, INI);
    gps("keys Sect1 n=0", "Sect1", NULL, "DEF", 0, INI);
    gps("keys Sect1 n=11", "Sect1", NULL, "DEF", 11, INI);
    gps("keys Sect1 n=5", "Sect1", NULL, "DEF", 5, INI);
    gps("keys Sect1 n=2", "Sect1", NULL, "DEF", 2, INI);
    gps("keys Sect1 n=1", "Sect1", NULL, "DEF", 1, INI);
    gps("keys Num n=200", "Num", NULL, "DEF", 200, INI);
    gps("keys NoSect", "NoSect", NULL, "DEF", 200, INI);
    gps("keys empty section", "Last", NULL, "DEF", 200, INI);

    /* integers */
    for (i = 1; i <= 29; i++) {
        char k[8];
        k[0] = 'n';
        if (i >= 10) {
            k[1] = (char)('0' + i / 10);
            k[2] = (char)('0' + i % 10);
            k[3] = 0;
        } else {
            k[1] = (char)('0' + i);
            k[2] = 0;
        }
        obs(nm("int Num/", k), GetPrivateProfileInt("Num", k, 77, INI));
    }
    obs("int missing (def -1)", GetPrivateProfileInt("Num", "none", -1, INI));
    obs("int missing (def 70000)", GetPrivateProfileInt("Num", "none", (int)70000L, INI));
    obs("int Sect1/key1", GetPrivateProfileInt("Sect1", "key1", 77, INI));
    obs("int Sect1/key6 (empty)", GetPrivateProfileInt("Sect1", "key6", 77, INI));
    obs("int missing file", GetPrivateProfileInt("Num", "n1", 77, "C:\\TKINI\\NONE.INI"));

    /* a long value */
    fill(longval, 1500, 'v');
    scpy(longval, "[Long]\r\nlong=");
    longval[13] = 'V';
    scpy(longval + 1500, "E\r\nafter=1\r\n");
    d_puts("C:\\TKINI\\TKL.INI", longval);
    gps("long n=2000", "Long", "long", "DEF", 2000, "C:\\TKINI\\TKL.INI");
    gps("long n=100", "Long", "long", "DEF", 100, "C:\\TKINI\\TKL.INI");
    gps("long after", "Long", "after", "DEF", 100, "C:\\TKINI\\TKL.INI");

    /* Ctrl-Z */
    d_puts("C:\\TKINI\\TKZ.INI", "[z]\r\na=1\r\n\x1A[z2]\r\nb=2\r\n");
    gps("ctrlz z/a", "z", "a", "DEF", 80, "C:\\TKINI\\TKZ.INI");
    gps("ctrlz z2/b", "z2", "b", "DEF", 80, "C:\\TKINI\\TKZ.INI");
    d_puts("C:\\TKINI\\TKZ2.INI", "[z]\r\na=1\r\n\x1A\r\n[z3]\r\nc=3\r\n");
    gps("ctrlz line z3/c", "z3", "c", "DEF", 80, "C:\\TKINI\\TKZ2.INI");
    d_puts("C:\\TKINI\\TKSP.INI", "[s]\r\n b4=4\r\n\tb5=5\r\n[t] x\r\nt=1\r\n");
    gps("leading space key b4", "s", "b4", "DEF", 80, "C:\\TKINI\\TKSP.INI");
    gps("leading space key ' b4'", "s", " b4", "DEF", 80, "C:\\TKINI\\TKSP.INI");
    gps("leading tab key b5", "s", "b5", "DEF", 80, "C:\\TKINI\\TKSP.INI");
    gps("section with text after ]", "t", "t", "DEF", 80, "C:\\TKINI\\TKSP.INI");
    gps("keys with leading space", "s", NULL, "DEF", 80, "C:\\TKINI\\TKSP.INI");

    /* NUL in the file */
    d_write("C:\\TKINI\\TKN.INI", "[n]\r\na=1\0x\r\nb=2\r\n", 18);
    gps("nul n/a", "n", "a", "DEF", 80, "C:\\TKINI\\TKN.INI");
    gps("nul n/b", "n", "b", "DEF", 80, "C:\\TKINI\\TKN.INI");
}

/* where a file without a path is looked for */
static void probe_where(void)
{
    d_puts("C:\\WINDOWS\\TKR.INI", "[r]\r\nw=win\r\n");
    d_puts("C:\\TKCWD\\TKR.INI", "[r]\r\nw=cwd\r\n");
    d_puts("C:\\WINDOWS\\SYSTEM\\TKR.INI", "[r]\r\nw=sys\r\n");
    gps("no path read", "r", "w", "DEF", 80, "TKR.INI");
    d_del("C:\\WINDOWS\\TKR.INI");
    gps("no path read, not in windir", "r", "w", "DEF", 80, "TKR.INI");
    gps("relative path read", "r", "w", "DEF", 80, ".\\TKR.INI");
    gps("lower case name", "r", "w", "DEF", 80, "c:\\tkcwd\\tkr.ini");
    d_del("C:\\TKCWD\\TKR.INI");
    d_del("C:\\WINDOWS\\SYSTEM\\TKR.INI");
    obs("no path write ret", WritePrivateProfileString("w", "k", "v", "TKW2.INI"));
    WritePrivateProfileString(NULL, NULL, NULL, "TKW2.INI");
    obs("no path write: in windir", d_exists("C:\\WINDOWS\\TKW2.INI"));
    obs("no path write: in cwd", d_exists("C:\\TKCWD\\TKW2.INI"));
    d_del("C:\\WINDOWS\\TKW2.INI");
    d_del("C:\\TKCWD\\TKW2.INI");
}

/* ---- profiles: writing ---- */

#define WINI "C:\\TKINI\\TKW.INI"

static void wps(const char *tag_, const char *sec, const char *key, const char *val,
                const char *file)
{
    char tag[100];
    scpy(tag, tag_);
    obs(nm(tag, " ret"), WritePrivateProfileString(sec, key, val, file));
}

/* flush the cached copy, then rewrite the file raw */
static void reset(const char *file, const char *text)
{
    WritePrivateProfileString(NULL, NULL, NULL, file);
    d_puts(file, text);
}

static void probe_write(void)
{
    reset(WINI, "[A]\r\na1=1\r\na2=2\r\n\r\n[B]\r\nb1=1\r\n");
    wps("modify a1", "A", "a1", "new", WINI);
    dumpfile("file after modify", WINI);
    wps("modify A2 (case)", "a", "A2", "X", WINI);
    dumpfile("file after case", WINI);
    wps("new key a3", "A", "a3", "3", WINI);
    dumpfile("file after new key", WINI);
    wps("new section C", "C", "c1", "c", WINI);
    dumpfile("file after new section", WINI);
    wps("delete b1", "B", "b1", NULL, WINI);
    dumpfile("file after delete key", WINI);
    wps("delete b1 again", "B", "b1", NULL, WINI);
    wps("delete missing section key", "Z", "z", NULL, WINI);
    wps("spaces", "B", "b2", "  spaced  ", WINI);
    wps("empty value", "B", "b3", "", WINI);
    wps("key with spaces", "B", " b4 ", "4", WINI);
    wps("section with spaces", " D ", "d", "1", WINI);
    dumpfile("file after spaces", WINI);
    gps("read b2", "B", "b2", "DEF", 80, WINI);
    gps("read b3", "B", "b3", "DEF", 80, WINI);
    gps("read b4", "B", "b4", "DEF", 80, WINI);
    gps("read D", "D", "d", "DEF", 80, WINI);
    wps("delete section A", "A", NULL, NULL, WINI);
    dumpfile("file after delete section", WINI);
    wps("delete section A again", "A", NULL, NULL, WINI);
    wps("NULL key with value", "B", NULL, "x", WINI);
    dumpfile("file after NULL key with value", WINI);
    wps("delete last section", "D", NULL, NULL, WINI);
    dumpfile("file after delete last", WINI);
    wps("value with CRLF", "B", "b5", "x\r\ny=z", WINI);
    gps("read b5", "B", "b5", "DEF", 80, WINI);
    gps("read y", "B", "y", "DEF", 80, WINI);
    dumpfile("file at end", WINI);

    /* the empty section [] */
    reset(WINI, "[]\r\ne=1\r\n");
    wps("empty section name", "", "f", "2", WINI);
    dumpfile("file empty section", WINI);

    /* no CRLF at the end */
    reset(WINI, "[t]\r\nx=1");
    wps("no CRLF at end", "t", "y", "2", WINI);
    dumpfile("file no CRLF", WINI);
    reset(WINI, "[t]\r\nx=1");
    wps("no CRLF new section", "u", "y", "2", WINI);
    dumpfile("file no CRLF new section", WINI);

    /* comments and blank lines around */
    reset(WINI, "; top\r\n[s]\r\n; c1\r\nk=1\r\n; c2\r\n\r\n\r\n[s2]\r\nq=1\r\n");
    wps("comments add", "s", "k2", "2", WINI);
    wps("comments modify", "s", "k", "9", WINI);
    dumpfile("file comments", WINI);
    reset(WINI, "[s]\r\n  k  =  1  \r\n");
    wps("spaced modify", "s", "k", "2", WINI);
    dumpfile("file spaced modify", WINI);
    reset(WINI, "[s]\r\na=1\r\n[s]\r\nb=1\r\n");
    wps("twice: modify in second", "s", "b", "2", WINI);
    dumpfile("file twice", WINI);
    reset(WINI, "[s]\r\na=1\r\n");
    wps("quotes", "s", "q", "\"x\"", WINI);
    wps("leading space value", "s", "l", "  y", WINI);
    wps("key with =", "s", "k=1", "z", WINI);
    wps("tab value", "s", "t", "\tx\t", WINI);
    dumpfile("file quotes", WINI);
    gps("read quotes", "s", "q", "DEF", 80, WINI);
    gps("read key with =", "s", "k", "DEF", 80, WINI);
    WritePrivateProfileString(NULL, NULL, NULL, WINI);
    gps("read quotes after flush", "s", "q", "DEF", 80, WINI);
    gps("read leading space after flush", "s", "l", "DEF", 80, WINI);
    reset(WINI, "[s]\r\na=1\r\n");
    wps("leading space key", "s", " b4 ", "4", WINI);
    wps("leading space section", " D ", "d", "1", WINI);
    gps("read b4 cached", "s", "b4", "DEF", 80, WINI);
    gps("read ' b4' cached", "s", " b4", "DEF", 80, WINI);
    WritePrivateProfileString(NULL, NULL, NULL, WINI);
    gps("read b4 after flush", "s", "b4", "DEF", 80, WINI);
    gps("read D after flush", "D", "d", "DEF", 80, WINI);
    dumpfile("file leading spaces", WINI);
    reset(WINI, "");
    wps("empty file", "e", "k", "v", WINI);
    dumpfile("file was empty", WINI);
    reset(WINI, "\r\n\r\n");
    wps("blank lines file", "e", "k", "v", WINI);
    dumpfile("file was blank lines", WINI);
    reset(WINI, "[e]\r\nk=1\r\n\x1A");
    wps("ctrl-z file", "e", "k2", "v", WINI);
    dumpfile("file ctrl-z", WINI);
    reset(WINI, "[e]\nk=1\n[f]\nm=1\n");
    wps("LF file", "e", "k2", "v", WINI);
    wps("LF file modify", "f", "m", "2", WINI);
    /* the writer assumes CR LF line ends: this file comes out mangled */
    dumpfile("~file LF", WINI);
    reset(WINI, "  ; c\r\n [s]  \r\n  x  =  1  \r\n   \r\n\t; c2 \r\ny\r\n\t[t] z\r\nq = \" 1 \"\r\n");
    wps("normalise", "t", "w", "2", WINI);
    dumpfile("file normalised", WINI);

    /* a new file, and an empty one */
    d_del("C:\\TKINI\\TKNEW.INI");
    wps("new file", "N", "k", "v", "C:\\TKINI\\TKNEW.INI");
    WritePrivateProfileString(NULL, NULL, NULL, "C:\\TKINI\\TKNEW.INI");
    dumpfile("file new", "C:\\TKINI\\TKNEW.INI");
    d_del("C:\\TKINI\\TKNEW.INI");
    wps("new file, delete", "N", "k", NULL, "C:\\TKINI\\TKNEW.INI");
    WritePrivateProfileString(NULL, NULL, NULL, "C:\\TKINI\\TKNEW.INI");
    dumpfile("new file after delete", "C:\\TKINI\\TKNEW.INI");
    d_del("C:\\TKINI\\TKNEW.INI");
    wps("bad directory", "N", "k", "v", "C:\\TKNODIR\\X.INI");
}

/* ---- caching ---- */
#define CINI "C:\\TKINI\\TKC.INI"

static void probe_cache(void)
{
    WORD date, time;
    d_puts(CINI, "[c]\r\nv=1\r\n");
    gps("cache 1", "c", "v", "DEF", 80, CINI);
    d_stamp(CINI, &date, &time);
    d_puts(CINI, "[c]\r\nv=2\r\n");
    d_setstamp(CINI, date, time);
    gps("cache same size same stamp", "c", "v", "DEF", 80, CINI);
    d_puts(CINI, "[c]\r\nv=33\r\n");
    d_setstamp(CINI, date, time);
    gps("cache other size same stamp", "c", "v", "DEF", 80, CINI);
    d_puts(CINI, "[c]\r\nv=4\r\n");
    d_setstamp(CINI, date, time + 1);
    gps("cache same size other stamp", "c", "v", "DEF", 80, CINI);
    /* another file in between */
    gps("cache other file", "Sect1", "key1", "DEF", 80, INI);
    d_puts(CINI, "[c]\r\nv=5\r\n");
    d_setstamp(CINI, date, time + 1);
    gps("cache after other file", "c", "v", "DEF", 80, CINI);

    wps("cache write", "c", "w", "9", CINI);
    dumpfile("cache file after write", CINI);
    gps("cache read written", "c", "w", "DEF", 80, CINI);
    obs("cache flush ret", WritePrivateProfileString(NULL, NULL, NULL, CINI));
    dumpfile("cache file after flush", CINI);
    /* change on disk, then write through the API */
    d_puts(CINI, "[c]\r\nv=6\r\nx=1\r\n");
    wps("cache write after disk change", "c", "w", "8", CINI);
    WritePrivateProfileString(NULL, NULL, NULL, CINI);
    dumpfile("cache file after disk change + write", CINI);

    /* WIN.INI */
    obs("WriteProfileString", WriteProfileString("TK14", "k", "v"));
    {
        static char w[8192];
        WORD n = d_read("C:\\WINDOWS\\WIN.INI", w, sizeof w), i, found = 0;
        for (i = 0; n != 0xFFFF && i + 6 <= n; i++)
            if (same_n(w + i, "[TK14]", 6))
                found = 1;
        obs("WIN.INI has [TK14] before flush", found);
        obs("WriteProfileString(NULL,NULL,NULL)", WriteProfileString(NULL, NULL, NULL));
        n = d_read("C:\\WINDOWS\\WIN.INI", w, sizeof w);
        found = 0;
        for (i = 0; n != 0xFFFF && i + 6 <= n; i++)
            if (same_n(w + i, "[TK14]", 6)) {
                found = 1;
                obs_b("WIN.INI from [TK14]", w + i, n - i);
                break;
            }
        obs("WIN.INI has [TK14] after flush", found);
        obs("WIN.INI ends in CRLF", n >= 2 && n != 0xFFFF && w[n - 2] == '\r' && w[n - 1] == '\n');
    }
    fill(buf, 80, '~');
    obs("GetProfileString ret", GetProfileString("TK14", "k", "D", buf, 80));
    obs_s("GetProfileString", buf);
    obs("GetProfileInt", GetProfileInt("TK14", "k", 5));
    WriteProfileString("TK14", "n", " 12 ");
    obs("GetProfileInt n", GetProfileInt("TK14", "n", 5));
    fill(buf, 80, '~');
    obs("GetProfileString keys ret", GetProfileString("TK14", NULL, "D", buf, 80));
    obs_b("GetProfileString keys", buf, 8);
    obs("WriteProfileString delete section", WriteProfileString("TK14", NULL, NULL));
    WriteProfileString(NULL, NULL, NULL);
    obs("GetProfileString after delete", GetProfileString("TK14", "k", "D", buf, 80));
}

/* ---- files ---- */

static OFSTRUCT of;

static void obs_of(const char *tag_, int r)
{
    char tag[100];
    scpy(tag, tag_);
    obs(nm(tag, " ret==-1"), r == -1);
    obs(nm(tag, " cBytes"), of.cBytes);
    obs(nm(tag, " fFixedDisk"), of.fFixedDisk);
    obs(nm(tag, " nErrCode"), of.nErrCode);
    obs_s(nm(tag, " szPathName"), of.szPathName);
}

static void close_r(int r)
{
    if (r != -1)
        _lclose(r);
}

static void probe_search(void)
{
    static const char *const where[] = {
        "C:\\TKCWD\\TKSRCH.TXT", "C:\\WINDOWS\\TKSRCH.TXT",
        "C:\\WINDOWS\\SYSTEM\\TKSRCH.TXT", "C:\\TKSRCH.TXT",
        "C:\\TKPATH\\TKSRCH.TXT", 0
    };
    WORD i, k, step;
    int r;
    for (i = 0; where[i]; i++)
        d_puts(where[i], where[i]);
    for (step = 0; step < 6; step++) {
        fill((char FAR *)&of, sizeof of, 0x55);
        r = OpenFile("TKSRCH.TXT", &of, OF_EXIST);
        for (k = 0; where[k]; k++)
            if (same_n(of.szPathName, where[k], slen(where[k]) + 1))
                break;
        obsn("search step found", step, r == -1 ? 0xFFFF : k);
        if (r == -1) {
            obsn("search step nErrCode", step, of.nErrCode);
            obs_s("search not found szPathName", of.szPathName);
            break;
        }
        d_del(where[k]);
    }
    /* _lopen does not search? */
    d_puts("C:\\TKPATH\\TKSRCH.TXT", "x");
    r = _lopen("TKSRCH.TXT", OF_READ);
    obs("_lopen of a file on the PATH", r == -1);
    close_r(r);
    d_puts("C:\\WINDOWS\\TKSRCH.TXT", "x");
    r = _lopen("TKSRCH.TXT", OF_READ);
    obs("_lopen of a file in windir", r == -1);
    close_r(r);
    r = OpenFile("TKSRCH.TXT", &of, OF_READ);
    obs_of("OpenFile in windir", r);
    close_r(r);
    r = OpenFile("TKSRCH.TXT", &of, OF_READ | OF_SEARCH);
    obs_of("OpenFile OF_SEARCH", r);
    close_r(r);
    d_del("C:\\TKPATH\\TKSRCH.TXT");
    d_del("C:\\WINDOWS\\TKSRCH.TXT");
    /* a relative path is not searched? */
    d_mkdir("C:\\WINDOWS\\TKSUB");
    d_puts("C:\\WINDOWS\\TKSUB\\TKS.TXT", "x");
    r = OpenFile("TKSUB\\TKS.TXT", &of, OF_EXIST);
    obs_of("OpenFile relative path in windir", r);
    d_del("C:\\WINDOWS\\TKSUB\\TKS.TXT");
    dos_p(0x3A00, 0, "C:\\WINDOWS\\TKSUB", 0);
}

static void probe_openfile(void)
{
    static const char *const parse[] = {
        "foo.txt", "FOO.TXT", "c:foo.txt", "\\foo.txt", "..\\foo.txt",
        "sub\\..\\foo.txt", "c:\\a\\b.txt", "a/b.txt", "toolongname.txtx",
        "foo", "foo.", "  foo.txt", ".\\foo.txt", "c:\\", "a:x.txt",
        "z:\\x.txt", "foo bar.txt", "C:\\TKCWD\\.\\x", "*.txt", "", 0
    };
    WORD i, date, time, sz;
    int r, r2;
    long l;

    d_puts("C:\\TKCWD\\F.TXT", "0123456789");
    fill((char FAR *)&of, sizeof of, 0x55);
    r = OpenFile("f.txt", &of, OF_READ);
    obs_of("OpenFile(f.txt, OF_READ)", r);
    obs_env("OpenFile handle", (WORD)r);
    d_stamp("C:\\TKCWD\\F.TXT", &date, &time);
    obs("reserved[0..1]==date", *(WORD *)&of.reserved[0] == date);
    obs("reserved[2..3]==time", *(WORD *)&of.reserved[2] == time);
    obs("reserved[0..1]==time", *(WORD *)&of.reserved[0] == time);
    obs("reserved[2..3]==date", *(WORD *)&of.reserved[2] == date);
    fill(buf, 20, '~');
    obs("_lread 4", _lread(r, buf, 4));
    obs_b("_lread 4 data", buf, 5);
    obs("_lread 100 (6 left)", _lread(r, buf, 100));
    obs("_lread at EOF", _lread(r, buf, 100));
    obs("_lwrite on read-only handle", _lwrite(r, "x", 1));
    obs("_lclose", _lclose(r));
    obs("_lclose again", _lclose(r));
    obs("_lread closed", _lread(r, buf, 4));
    obs("_lread bad handle", _lread(200, buf, 4));

    fill((char FAR *)&of, sizeof of, 0x55);
    r = OpenFile("nofile.txt", &of, OF_READ);
    obs_of("OpenFile(nofile.txt)", r);
    obs("nofile reserved[0]", of.reserved[0]);
    fill((char FAR *)&of, sizeof of, 0x55);
    r = OpenFile("C:\\TKNODIR\\x.txt", &of, OF_READ);
    obs_of("OpenFile(no dir)", r);
    fill((char FAR *)&of, sizeof of, 0x55);
    r = OpenFile("f.txt", &of, OF_EXIST);
    obs_of("OpenFile(f.txt, OF_EXIST)", r);
    obs("OF_EXIST handle closed", _lclose(r));
    fill((char FAR *)&of, sizeof of, 0x55);
    r = OpenFile("nofile.txt", &of, OF_EXIST);
    obs_of("OpenFile(nofile, OF_EXIST)", r);

    for (i = 0; parse[i]; i++) {
        fill((char FAR *)&of, sizeof of, 0x55);
        r = OpenFile(parse[i], &of, OF_PARSE);
        if (r == -1) {
            /* a bad name: 3.1 leaves a partial path that w16 does not
               reproduce (not checked) */
            char t[60];
            scpy(t, nm("OF_PARSE fails ", parse[i]));
            obs(t, of.nErrCode);
            obs_b(nm("~OF_PARSE partial ", parse[i]), of.szPathName, slen(of.szPathName));
            obs(nm("OF_PARSE fails cBytes untouched ", parse[i]), of.cBytes == 0x55);
        } else
            obs_of(nm("OF_PARSE ", parse[i]), r);
        if (r != -1)
            obs(nm("OF_PARSE ret ", parse[i]), (WORD)r);
    }

    /* OF_REOPEN uses szPathName */
    fill((char FAR *)&of, sizeof of, 0x55);
    r = OpenFile("f.txt", &of, OF_EXIST);
    r = OpenFile("whatever.txt", &of, OF_REOPEN | OF_READ);
    obs_of("OF_REOPEN", r);
    close_r(r);
    /* OF_VERIFY after the date changed */
    d_setstamp("C:\\TKCWD\\F.TXT", date, time + 1);
    r = OpenFile("f.txt", &of, OF_REOPEN | OF_VERIFY | OF_READ);
    obs_of("OF_REOPEN|OF_VERIFY changed", r);
    close_r(r);
    r = OpenFile("f.txt", &of, OF_REOPEN | OF_VERIFY | OF_READ);
    obs_of("OF_REOPEN|OF_VERIFY same", r);
    close_r(r);
    d_setstamp("C:\\TKCWD\\F.TXT", date + 1, time);
    fill(of.szPathName + 20, 4, 0x55);
    r = OpenFile("f.txt", &of, OF_REOPEN | OF_VERIFY | OF_READ);
    obs_of("OF_VERIFY date changed", r);
    close_r(r);
    r = OpenFile("f.txt", &of, OF_VERIFY | OF_READ);
    obs_of("OF_VERIFY without OF_REOPEN", r);
    close_r(r);
    r = OpenFile("f.txt", &of, OF_READ);
    of.reserved[0] ^= 1;
    close_r(r);
    r = OpenFile("whatever", &of, OF_REOPEN | OF_EXIST | OF_VERIFY);
    obs_of("OF_VERIFY|OF_EXIST reserved changed", r);
    d_setstamp("C:\\TKCWD\\F.TXT", date, time);

    /* OF_CREATE truncates */
    r = OpenFile("f.txt", &of, OF_CREATE | OF_WRITE);
    obs_of("OF_CREATE existing", r);
    obs("OF_CREATE write", _lwrite(r, "abc", 3));
    close_r(r);
    obs("size after OF_CREATE", d_size("C:\\TKCWD\\F.TXT"));
    r = OpenFile("g.txt", &of, OF_CREATE);
    obs_of("OF_CREATE new, mode 0", r);
    obs("OF_CREATE mode 0 write", _lwrite(r, "abc", 3));
    close_r(r);
    obs("g.txt size", d_size("C:\\TKCWD\\G.TXT"));
    r = OpenFile("g.txt", &of, OF_DELETE);
    obs_of("OF_DELETE", r);
    obs_env("OF_DELETE ret (a handle number)", (WORD)r);
    obs("g.txt exists after", d_exists("C:\\TKCWD\\G.TXT"));
    r = OpenFile("g.txt", &of, OF_DELETE);
    obs_of("OF_DELETE missing", r);
    d_puts("C:\\TKPATH\\TKDEL.TXT", "x");
    r = OpenFile("TKDEL.TXT", &of, OF_DELETE);
    obs_of("OF_DELETE on PATH", r);
    obs("TKDEL exists after", d_exists("C:\\TKPATH\\TKDEL.TXT"));
    d_del("C:\\TKPATH\\TKDEL.TXT");

    /* read-only files */
    r = _lcreat("C:\\TKCWD\\RO.TXT", 1);
    obs("_lcreat read-only ret==-1", r == -1);
    obs("_lcreat read-only write", _lwrite(r, "ro", 2));
    close_r(r);
    r = _lopen("C:\\TKCWD\\RO.TXT", OF_WRITE);
    obs("_lopen RO for write", r == -1);
    close_r(r);
    r = OpenFile("RO.TXT", &of, OF_WRITE);
    obs_of("OpenFile RO for write", r);
    close_r(r);
    r = OpenFile("RO.TXT", &of, OF_DELETE);
    obs_of("OF_DELETE read-only", r);
    d_setattr("C:\\TKCWD\\RO.TXT", 0);
    d_del("C:\\TKCWD\\RO.TXT");
    r = _lcreat("C:\\TKCWD\\HID.TXT", 2);
    close_r(r);
    {
        WORD a = 0;
        dos(0x4300, 0, 0, OFFSETOF("C:\\TKCWD\\HID.TXT"), SELECTOROF("C:\\TKCWD\\HID.TXT"), 0, &a, 0);
        obs("_lcreat attr 2 -> attributes", a);
    }
    d_setattr("C:\\TKCWD\\HID.TXT", 0);
    d_del("C:\\TKCWD\\HID.TXT");
    r = _lcreat("C:\\TKNODIR\\X.TXT", 0);
    obs("_lcreat bad dir", r == -1);

    /* _lcreat truncates; _lwrite 0 truncates; _llseek */
    r = _lcreat("C:\\TKCWD\\L.TXT", 0);
    obs("_lwrite 10", _lwrite(r, "0123456789", 10));
    l = _llseek(r, 0, 1);
    obs("_llseek(0,1)", (WORD)l);
    l = _llseek(r, 4, 0);
    obs("_llseek(4,0)", (WORD)l);
    obs("_lwrite 0", _lwrite(r, "", 0));
    l = _llseek(r, 0, 2);
    obs("_llseek(0,2) after _lwrite 0", (WORD)l);
    l = _llseek(r, -2, 2);
    obs("_llseek(-2,2)", (WORD)l);
    l = _llseek(r, -10, 0);
    obs("_llseek(-10,0) lo", (WORD)l);
    obs("_llseek(-10,0) hi", (WORD)(l >> 16));
    l = _llseek(r, 0, 1);
    obs("_llseek(0,1) after", (WORD)l);
    l = _llseek(r, 1, 3);
    obs("_llseek(1,3) lo", (WORD)l);
    obs("_llseek(1,3) hi", (WORD)(l >> 16));
    l = _llseek(r, 100, 0);
    obs("_llseek(100,0)", (WORD)l);
    obs("_lwrite past end", _lwrite(r, "Z", 1));
    l = _llseek(200, 0, 0);
    obs("_llseek bad handle lo", (WORD)l);
    obs("_llseek bad handle hi", (WORD)(l >> 16));
    _lclose(r);
    obs("L.TXT size", d_size("C:\\TKCWD\\L.TXT"));
    r = _lcreat("C:\\TKCWD\\L.TXT", 0);
    _lclose(r);
    obs("L.TXT size after _lcreat", d_size("C:\\TKCWD\\L.TXT"));
    r = _lopen("C:\\TKCWD\\L.TXT", OF_READWRITE);
    obs("_lopen READWRITE ret==-1", r == -1);
    obs("_lopen write", _lwrite(r, "rw", 2));
    _llseek(r, 0, 0);
    obs("_lopen read", _lread(r, buf, 10));
    _lclose(r);
    r = _lopen("C:\\TKCWD\\L.TXT", OF_READ | OF_SHARE_EXCLUSIVE);
    r2 = _lopen("C:\\TKCWD\\L.TXT", OF_READ);
    obs("_lopen share exclusive, second open", r2 == -1);
    close_r(r2);
    close_r(r);
    r = _lopen("C:\\TKCWD\\L.TXT", 0x80);
    obs("_lopen mode 80h", r == -1);
    close_r(r);
    r = _lopen("C:\\TKCWD\\L.TXT", 3);
    obs("_lopen mode 3", r == -1);
    close_r(r);
    r = _lopen("C:\\TKCWD\\NOFILE.TXT", OF_READ);
    obs("_lopen missing", (WORD)r);
    d_del("C:\\TKCWD\\L.TXT");
    sz = 0;
    (void)sz;
}

static void probe_huge(void)
{
    HGLOBAL g = GlobalAlloc(GMEM_FIXED, 0x11000L);
    WORD sel = SELECTOROF(GlobalLock(g)), inc = get_ahincr(), i, bad = 0;
    long n;
    int r;
    BYTE FAR *p;
    for (i = 0; i < 0x8800; i++) {
        p = (BYTE FAR *)MAKELP(sel + (i >= 0x8000 ? inc : 0), (WORD)(2 * i));
        p[0] = (BYTE)i;
        p[1] = (BYTE)(i >> 8);
    }
    r = _lcreat("C:\\TKCWD\\H.TXT", 0);
    n = _hwrite(r, MAKELP(sel, 0), 0x11000L);
    obs("_hwrite 11000h lo", (WORD)n);
    obs("_hwrite 11000h hi", (WORD)(n >> 16));
    n = _hwrite(r, MAKELP(sel, 0), 0L);
    obs("_hwrite 0", (WORD)n);
    _lclose(r);
    for (i = 0; i < 0x8800; i++) {
        p = (BYTE FAR *)MAKELP(sel + (i >= 0x8000 ? inc : 0), (WORD)(2 * i));
        p[0] = p[1] = 0;
    }
    r = _lopen("C:\\TKCWD\\H.TXT", OF_READ);
    obs("H.TXT size lo", (WORD)_llseek(r, 0, 2));
    obs("H.TXT size hi", (WORD)(_llseek(r, 0, 2) >> 16));
    _llseek(r, 0, 0);
    n = _hread(r, MAKELP(sel, 0), 0x20000L);
    obs("_hread 20000h lo", (WORD)n);
    obs("_hread 20000h hi", (WORD)(n >> 16));
    for (i = 0; i < 0x8800; i++) {
        p = (BYTE FAR *)MAKELP(sel + (i >= 0x8000 ? inc : 0), (WORD)(2 * i));
        if (p[0] != (BYTE)i || p[1] != (BYTE)(i >> 8))
            bad++;
    }
    obs("_hread data bad words", bad);
    n = _hread(r, MAKELP(sel, 0), 10L);
    obs("_hread at EOF", (WORD)n);
    _lclose(r);
    n = _hread(200, MAKELP(sel, 0), 10L);
    obs("_hread bad handle lo", (WORD)n);
    obs("_hread bad handle hi", (WORD)(n >> 16));
    n = _hwrite(200, MAKELP(sel, 0), 10L);
    obs("_hwrite bad handle lo", (WORD)n);
    obs("_hwrite bad handle hi", (WORD)(n >> 16));
    d_del("C:\\TKCWD\\H.TXT");
    GlobalUnlock(g);
    GlobalFree(g);
}

typedef WORD (FAR PASCAL *TEMPDRIVE)(char);

static void probe_temp(void)
{
    static char t[160], e[40];
    static const char *const pre[] = { "TKP", "abc", "ABCDEF", "", "A", 0 };
    WORD i, r;
    TEMPDRIVE td = (TEMPDRIVE)GetTempDrive;

    obs("GetTempDrive(0)", td(0));
    obs("GetTempDrive('A')", td('A'));
    obs("GetTempDrive('D')", td('D'));
    for (i = 0; pre[i]; i++) {
        fill(t, sizeof t, '~');
        r = GetTempFileName(0, pre[i], 0x12AB, t);
        obs(nm("GetTempFileName(0,pre,12AB) ret ", pre[i]), r);
        obs_s(nm("GetTempFileName(0,pre,12AB) ", pre[i]), t);
        obs(nm("  created ", pre[i]), d_exists(t));
    }
    fill(t, sizeof t, '~');
    r = GetTempFileName('C' | TF_FORCEDRIVE, "TKP", 0x12AB, t);
    obs("GetTempFileName(C|80h) ret", r);
    obs_s("GetTempFileName(C|80h)", t);
    fill(t, sizeof t, '~');
    r = GetTempFileName('C', "TKP", 0x12AB, t);
    obs_s("GetTempFileName(C)", t);
    fill(t, sizeof t, '~');
    r = GetTempFileName('A', "TKP", 0x12AB, t);
    obs_s("GetTempFileName(A)", t);
    fill(t, sizeof t, '~');
    r = GetTempFileName(0, "TKP", 1, t);
    obs_s("GetTempFileName(unique 1)", t);
    fill(t, sizeof t, '~');
    r = GetTempFileName(0, "TKP", 0, t);
    obs_env("GetTempFileName(unique 0) ret", r);
    obs_s("GetTempFileName(unique 0)", t);
    obs("  created", d_exists(t));
    obs("  size", d_size(t));
    /* the name for that number */
    GetTempFileName(0, "TKP", r, e);
    obs("  name == the name for the number", same_n(e, t, slen(t) + 1));
    /* a second one with the file still there */
    r = GetTempFileName(0, "TKP", 0, e);
    obs("  second differs", !same_n(e, t, slen(t) + 1));
    d_del(t);
    d_del(e);
}

static void probe_misc(void)
{
    static const WORD drv[] = { 0, 1, 2, 4, 25, 26, 0xFFFF };   /* not D: (w16's test host uses it) */
    WORD i;
    obs_env("BIOS equipment word", *w_at(get_0040h(), 0x10));
    for (i = 0; i < sizeof drv / sizeof drv[0]; i++)
        obsn("GetDriveType", drv[i], GetDriveType(drv[i]));
    obs("SetHandleCount(0)", SetHandleCount(0));
    obs("SetHandleCount(10)", SetHandleCount(10));
    obs("SetHandleCount(20)", SetHandleCount(20));
    obs("SetHandleCount(30)", SetHandleCount(30));
    obs("SetHandleCount(25)", SetHandleCount(25));
    obs("SetHandleCount(255)", SetHandleCount(255));
    obs("SetHandleCount(300)", SetHandleCount(300));
    obs("SetHandleCount(0) after", SetHandleCount(0));
    {
        /* opening more than 20 files */
        static int hs[40];
        WORD n = 0;
        for (i = 0; i < 40; i++) {
            hs[i] = _lopen("C:\\TKCWD\\F.TXT", OF_READ);
            if (hs[i] != -1)
                n++;
        }
        obs("files opened of 40 after SetHandleCount(300)", n);
        for (i = 0; i < 40; i++)
            close_r(hs[i]);
    }
    fill(buf, 200, '~');
    obs("GetWindowsDirectory", GetWindowsDirectory(buf, 200));
    obs_s("GetWindowsDirectory", buf);
    fill(buf, 200, '~');
    obs("GetSystemDirectory", GetSystemDirectory(buf, 200));
    obs_s("GetSystemDirectory", buf);
}

/* last: may not be supported */
static void probe_null_section(void)
{
    gps("sections (NULL section) n=300", NULL, NULL, "DEF", 300, INI);
    gps("NULL section, key1", NULL, "key1", "DEF", 300, INI);
}

int __cdecl tmain(void)
{
    out("TK14 start\r\n");
    d_mkdir("C:\\TKINI");
    d_mkdir("C:\\TKCWD");
    d_mkdir("C:\\TKPATH");
    d_mkdir("C:\\TKTMP");
    d_chdir("C:\\TKCWD");
    dos(0x0E02, 0, 0, 2, 0, 0, 0, 0);           /* drive C: */
    probe_read();
    probe_where();
    probe_write();
    probe_cache();
    probe_search();
    probe_openfile();
    probe_huge();
    probe_temp();
    probe_misc();
    probe_null_section();
    out("TK14 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
