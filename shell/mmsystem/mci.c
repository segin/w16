/*
 * mci.c - w16's MMSYSTEM.DLL, layer 4: the Media Control Interface
 * (ARCHITECTURE.md 23; docs/notes/mmsystem-handover.md has the
 * observations this follows). The sixteen MCI exports (701-717).
 *
 * Sources: Multimedia Programmer's Guide ch. 2 and 4 (the functions, the
 * command messages, flags and parameter blocks, [mci] and [mci
 * extensions], device types and names, MCI_ALL_DEVICE_ID, wait, notify,
 * break, sysinfo); Wine's mmddk.h of before 2002 (X11 licence) for the
 * driver side's names and numbers (MCI_OPEN_DRIVER_PARMS, the command
 * table item types, the MCI_*_RETURNED flags of a driver's answer). The
 * behaviour was observed on Windows 3.1 with MMP's section Q and the test
 * driver MCT.DRV (tests/mmsystem/mmci.c, mct.c) and with 3.1's MCIWAVE
 * and MCISEQ behind logging drivers:
 *   - a device is opened with USER's OpenDriver(file, NULL, lParam) on its
 *     [mci] line's file, lParam the MCI_OPEN_DRIVER_PARMS (the new device
 *     ID, the line's options, a command table and the device type for the
 *     driver to fill), then MCI_OPEN_DRIVER (0801h) with the open's flags
 *     and block; commands go by SendDriverMessage; a close is
 *     MCI_CLOSE_DRIVER (0802h) and CloseDriver;
 *   - a command string is parsed with the device's own table, then the
 *     table of the type it reported, then the core table (w16's tables:
 *     tools/mkmcitab.py), into a zeroed block in MMSYSTEM's heap whose
 *     strings point into MMSYSTEM's copy of the command;
 *   - the return value is turned into the return string as 3.1 does.
 */
#include "mmsys.h"

#define MM_MCINOTIFY        0x03B9
#define MCI_NOTIFY          0x00000001L
#define MCI_WAIT            0x00000002L
#define MCI_OPEN_DRIVER     0x0801
#define MCI_CLOSE_DRIVER    0x0802
#define MCI_OPEN            0x0803
#define MCI_CLOSE           0x0804
#define MCI_SYSINFO         0x0810
#define MCI_BREAK           0x0811
#define MCI_SOUND           0x0812
#define MCI_ALL_DEVICE_ID   0xFFFF
#define MCI_OPEN_SHAREABLE  0x0100L
#define MCI_OPEN_ELEMENT    0x0200L
#define MCI_OPEN_ALIAS      0x0400L
#define MCI_OPEN_ELEMENT_ID 0x0800L
#define MCI_OPEN_TYPE_ID    0x1000L
#define MCI_OPEN_TYPE       0x2000L
#define MCI_SYSINFO_QUANTITY    0x0100L
#define MCI_SYSINFO_OPEN        0x0200L
#define MCI_SYSINFO_NAME        0x0400L
#define MCI_SYSINFO_INSTALLNAME 0x0800L
#define MCI_BREAK_KEY       0x0100L
#define MCI_BREAK_HWND      0x0200L
#define MCI_BREAK_OFF       0x0400L
#define MCI_SOUND_NAME      0x0100L
#define MCI_NOTIFY_SUCCESSFUL 1
#define MCI_DEVTYPE_FIRST   513
#define MCI_DEVTYPE_LAST    523
#define MCI_STRING_OFFSET   512
#define MCI_NO_COMMAND_TABLE 0xFFFF

/* a driver's answer (Wine's mmddk.h of before 2002) */
#define MCI_RESOURCE_RETURNED   0x00010000L
#define MCI_COLONIZED3_RETURN   0x00020000L
#define MCI_COLONIZED4_RETURN   0x00040000L
#define MCI_INTEGER_RETURNED    0x00080000L
#define MCI_RESOURCE_DRIVER     0x00100000L

/* command table item types */
#define CT_HEAD     0
#define CT_STRING   1
#define CT_INTEGER  2
#define CT_END      3
#define CT_RETURN   4
#define CT_FLAG     5
#define CT_ENDLIST  6
#define CT_RECT     7
#define CT_CONST    8
#define CT_ENDCONST 9

/* MCI errors (MCIERR_BASE + n, the SDK's numbers) */
#define E_INVALID_DEVICE_ID     257
#define E_UNRECOGNIZED_KEYWORD  259
#define E_UNRECOGNIZED_COMMAND  261
#define E_HARDWARE              262
#define E_INVALID_DEVICE_NAME   263
#define E_OUT_OF_MEMORY         264
#define E_DEVICE_OPEN           265
#define E_CANNOT_LOAD_DRIVER    266
#define E_MISSING_COMMAND_STRING 267
#define E_PARAM_OVERFLOW        268
#define E_MISSING_STRING_ARGUMENT 269
#define E_BAD_INTEGER           270
#define E_MISSING_PARAMETER     273
#define E_DRIVER_CLOSED         278
#define E_CANNOT_USE_ALL        279
#define E_MULTIPLE              280
#define E_OUTOFRANGE            282
#define E_FLAGS_NOT_COMPATIBLE  284
#define E_DEVICE_TYPE_REQUIRED  287
#define E_DUPLICATE_ALIAS       289
#define E_BAD_CONSTANT          290
#define E_MISSING_DEVICE_NAME   292
#define E_NO_CLOSING_QUOTE      294
#define E_DUPLICATE_FLAGS       295
#define E_NULL_PARAMETER_BLOCK  297
#define E_NEW_REQUIRES_ALIAS    299
#define E_NOTIFY_ON_AUTO_OPEN   300
#define E_EXTRA_CHARACTERS      305
#define E_NO_INTEGER            312
#define E_CUSTOM_DRIVER_BASE    512

typedef UINT (FAR PASCAL *YIELDPROC)(UINT, DWORD);
typedef DWORD (FAR PASCAL *MMDRVPROC)(DWORD, HDRVR, UINT, LPARAM, LPARAM);

extern HWND __cdecl snd_window(void);           /* snd.c: MMSYSTEM's window */
extern BOOL MMAPI SNDPLAYSOUND(LPCSTR, UINT);   /* snd.c */

/* ---- devices ---- */
#define NDEV 64
struct mcidev {
    HDRVR hdrv;
    HTASK task;             /* the opener */
    WORD type;              /* the type the driver reported */
    WORD custom;            /* its own command table (or FFFFh) */
    WORD typetab;           /* the table of its type (or FFFFh) */
    WORD autoopen;          /* opened by mciSendString for one command */
    YIELDPROC yproc;
    DWORD ydata;
    WORD breakoff;
    HWND breakwnd;
    DWORD drvdata;
    DWORD elementid;
    char name[80];          /* the [mci] key (sysinfo installname) */
    char alias[80];
};
static struct mcidev *devs[NDEV + 1];      /* by device ID */

/* ---- command tables: 0 the core table, then the loaded ones ---- */
#define NTAB 32
static struct {
    HGLOBAL h;
    LPCSTR p;
    WORD type;
    WORD used;
    WORD refs;              /* a type's table: the devices using it */
} tabs[NTAB];
/* a type's table takes the lowest free number like a driver's and goes
   with the last device of the type (MMP: MCIWAVE's table was 1 or 2 by
   whether a sequencer was open) */
static WORD typetabs[MCI_DEVTYPE_LAST - MCI_DEVTYPE_FIRST + 1];  /* index + 1 */
static const char *const typenames[] = {
    "vcr", "videodisc", "overlay", "cdaudio", "dat", "scanner", "animation",
    "digitalvideo", "other", "waveaudio", "sequencer"
};

UINT MMAPI MCIDRIVERYIELD(UINT dev);
DWORD MMAPI MCISENDCOMMAND(UINT dev, UINT msg, DWORD flags, DWORD parms);

static struct mcidev *getdev(UINT id)
{
    return id >= 1 && id <= NDEV ? devs[id] : 0;
}

static int blank(char c)
{
    return c == ' ' || c == '\t';
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int haschar(LPCSTR s, char c)
{
    while (*s)
        if (*s++ == c)
            return 1;
    return 0;
}

static int sameword(LPCSTR a, LPCSTR b)
{
    while (*a && *b && lower(*a) == lower(*b))
        a++, b++;
    return !*a && !*b;
}

/* ---- the default yield procedure (mciSetYieldProc's default; MCI_BREAK
   sets its key): UNVERIFIED beyond what MMP saw (data VK_CANCEL, called
   directly it answers 0): the key, pressed while the break window (if one
   was given) is active, answers -1; otherwise 0 after letting the other
   tasks run */
UINT MMAPI mm_mciyield(UINT dev, DWORD data)
{
    struct mcidev *d = getdev(dev);
    MSG m;
    if (d && !d->breakoff && LOWORD(data) &&
        (GetAsyncKeyState(LOWORD(data)) & 0x8000) &&
        (!d->breakwnd || GetActiveWindow() == d->breakwnd))
        return (UINT)-1;
    PeekMessage(&m, 0, 0, 0, PM_NOREMOVE);
    return 0;
}

/* ---- tables ---- */
static LPCSTR nextitem(LPCSTR p, DWORD FAR *val, WORD FAR *type)
{
    while (*p)
        p++;
    p++;
    if (val)
        *val = *(DWORD FAR *)p;
    if (type)
        *type = *(WORD FAR *)(p + 4);
    return p + 6;
}

static UINT newtable(HGLOBAL h, WORD type, UINT from)
{
    UINT i;
    for (i = from; i < NTAB && tabs[i].used; i++)
        ;
    if (i == NTAB)
        return MCI_NO_COMMAND_TABLE;
    tabs[i].h = h;
    tabs[i].p = (LPCSTR)LockResource(h);
    tabs[i].type = type;
    tabs[i].used = 1;
    return i;
}

static void coretable(void)
{
    HRSRC r;
    if (tabs[0].used)
        return;
    r = FindResource(mm_hinst, "CORE", RT_RCDATA);
    if (r)
        newtable(LoadResource(mm_hinst, r), 0, 0);
}

/* the table of a device type (MMSYSTEM's own, by the type's name) */
static UINT typetable(WORD type)
{
    char name[16];
    HRSRC r;
    int k;
    UINT t;
    if (type < MCI_DEVTYPE_FIRST || type > MCI_DEVTYPE_LAST)
        return MCI_NO_COMMAND_TABLE;
    k = type - MCI_DEVTYPE_FIRST;
    if (typetabs[k]) {
        tabs[typetabs[k] - 1].refs++;
        return typetabs[k] - 1;
    }
    lstrcpy(name, typenames[k]);
    AnsiUpper(name);
    r = FindResource(mm_hinst, name, RT_RCDATA);
    if (!r)
        return MCI_NO_COMMAND_TABLE;
    coretable();
    t = newtable(LoadResource(mm_hinst, r), type, 1);
    if (t == MCI_NO_COMMAND_TABLE)
        return t;
    tabs[t].refs = 1;
    typetabs[k] = t + 1;
    return t;
}

static void untypetable(UINT t)
{
    int k;
    if (t >= NTAB || !tabs[t].used || --tabs[t].refs)
        return;
    for (k = 0; k <= MCI_DEVTYPE_LAST - MCI_DEVTYPE_FIRST; k++)
        if (typetabs[k] == t + 1)
            typetabs[k] = 0;
    UnlockResource(tabs[t].h);
    FreeResource(tabs[t].h);
    tabs[t].used = 0;
    tabs[t].p = 0;
}

/* a verb's entry in table t */
static LPCSTR findverb(UINT t, LPCSTR verb)
{
    LPCSTR p;
    DWORD v;
    WORD type;
    if (t >= NTAB || !tabs[t].used || !tabs[t].p)
        return 0;
    for (p = tabs[t].p;;) {
        LPCSTR item = p;
        p = nextitem(p, &v, &type);
        if (type == CT_ENDLIST)
            return 0;
        if (type == CT_HEAD && sameword(item, verb))
            return item;
    }
}

/* the verb's entry in a device's tables, its own first */
static LPCSTR devverb(struct mcidev *d, LPCSTR verb, int *core)
{
    LPCSTR e = 0;
    *core = 0;
    if (d) {
        e = findverb(d->custom, verb);
        if (!e && d->typetab != MCI_NO_COMMAND_TABLE)
            e = findverb(d->typetab, verb);
    }
    if (!e) {
        coretable();
        e = findverb(0, verb);
        *core = 1;
    }
    return e;
}

/* ---- the string parser ---- */
struct parse {
    LPDWORD blk;
    DWORD flags;
    int quirk;              /* "open" with a device's own entry: strings copied */
    HLOCAL copies[8];
    int ncopies;
};

/* a keyword at s: its words, blanks in between as in the input; returns
   the input after it, or 0 */
static LPSTR match(LPCSTR kw, LPSTR s)
{
    while (*kw) {
        if (*kw == ' ') {
            if (!blank(*s))
                return 0;
            while (blank(*s))
                s++;
            kw++;
            continue;
        }
        if (lower(*kw) != lower(*s))
            return 0;
        kw++;
        s++;
    }
    return *s && !blank(*s) ? 0 : s;
}

static LPSTR skipb(LPSTR s)
{
    while (blank(*s))
        s++;
    return s;
}

/* the next token at *ps: quoted (the quotes taken off) or up to a blank;
   ended with a NUL in place */
static DWORD token(LPSTR FAR *ps, LPSTR FAR *tok)
{
    LPSTR s = skipb(*ps);
    if (*s == '"') {
        LPSTR e = s + 1;
        while (*e && *e != '"')
            e++;
        if (!*e)
            return E_NO_CLOSING_QUOTE;
        if (e[1] && !blank(e[1]))
            return E_EXTRA_CHARACTERS;
        *tok = s + 1;
        *e = 0;
        *ps = e[1] ? e + 2 : e + 1;
        return 0;
    }
    *tok = s;
    while (*s && !blank(*s))
        s++;
    if (*s)
        *s++ = 0;
    *ps = s;
    return 0;
}

/* an integer as 3.1 reads one: decimal with a leading "-" (signed then),
   or up to four bytes "a:b:c:d"; a positive number wraps past 2^32
   unless a digit would multiply past 429496729 (MMP's numbers section) */
static int number(LPCSTR s, DWORD FAR *v)
{
    DWORD n = 0;
    int neg = 0, parts = 0;
    DWORD packed = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    if (*s < '0' || *s > '9')
        return 0;
    for (;;) {
        n = 0;
        if (*s < '0' || *s > '9')
            return 0;
        while (*s >= '0' && *s <= '9') {
            if (n > 429496729L)
                return 0;
            n = n * 10 + (*s++ - '0');
        }
        if (*s == ':') {
            if (neg || parts >= 3 || n > 255)
                return 0;
            packed |= n << (8 * parts++);
            s++;
            continue;
        }
        break;
    }
    if (*s)
        return 0;
    if (parts) {
        if (n > 255)
            return 0;
        *v = packed | (n << (8 * parts));
        return 1;
    }
    if (neg) {
        if (n > 0x7FFFFFFFL)
            return 0;
        *v = (DWORD)-(long)n;
    } else
        *v = n;
    return 1;
}

static DWORD storestr(struct parse *ps, DWORD FAR *slot, LPSTR tok)
{
    if (ps->quirk) {
        /* 3.1's "open" through a device's own entry: the string lands in
           a zeroed copy, but the block points 4 bytes into it (alias
           "abcdefghij" reaches the driver as "efghij", "ab" as "") */
        int n = lstrlen(tok);
        HLOCAL h = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, n + 5);
        if (!h)
            return E_OUT_OF_MEMORY;
        lstrcpy((LPSTR)(char NEAR *)h, tok);
        if (ps->ncopies < 8)
            ps->copies[ps->ncopies++] = h;
        *slot = (DWORD)(LPSTR)((char NEAR *)h + 4);
    } else
        *slot = (DWORD)tok;
    return 0;
}

/* the value of a constant at *ps: one of its names, or an integer */
static DWORD constval(LPCSTR p, LPSTR FAR *ps, DWORD FAR *v)
{
    LPSTR s = skipb(*ps), e, tok;
    DWORD val;
    WORD type;
    DWORD r;
    if (!*s)
        return E_NO_INTEGER;
    for (;;) {
        LPCSTR item = p;
        p = nextitem(p, &val, &type);
        if (type == CT_ENDCONST)
            break;
        if (*item && (e = match(item, s)) != 0) {
            *v = val;
            *ps = e;
            return 0;
        }
    }
    r = token(ps, &tok);
    if (r)
        return r;
    return number(tok, v) ? 0 : E_BAD_CONSTANT;
}

/* the items of a command entry from p (after its head and return):
   parse args into ps->blk */
static DWORD parseargs(LPCSTR entry, LPSTR args, struct parse *ps)
{
    LPCSTR p;
    LPSTR s = args, tok;
    DWORD val, r, v;
    WORD type, off;
    for (;;) {
        int done = 0;
        LPCSTR emptystr = 0, emptyconst = 0;
        WORD emptystroff = 0, emptyconstoff = 0;
        DWORD emptystrflag = 0, emptyconstflag = 0;
        s = skipb(s);
        if (!*s)
            return 0;
        p = nextitem(entry, 0, 0);
        off = 4;
        for (;;) {
            LPCSTR item = p;
            LPSTR e;
            p = nextitem(p, &val, &type);
            if (type == CT_END || type == CT_ENDLIST)
                break;
            switch (type) {
            case CT_RETURN:
                off += val == CT_INTEGER ? 4 : 8;
                break;
            case CT_FLAG:
                if (*item && (e = match(item, s)) != 0) {
                    if (ps->flags & val)
                        return E_DUPLICATE_FLAGS;
                    ps->flags |= val;
                    s = e;
                    done = 1;
                }
                break;
            case CT_INTEGER:
                if (*item && (e = match(item, s)) != 0) {
                    if (ps->flags & val)
                        return E_DUPLICATE_FLAGS;
                    s = e;
                    if (!*skipb(s))
                        return E_BAD_INTEGER;
                    r = token(&s, &tok);
                    if (r)
                        return r;
                    if (!number(tok, &v))
                        return E_BAD_INTEGER;
                    ps->blk[off / 4] = v;
                    ps->flags |= val;
                    done = 1;
                }
                off += 4;
                break;
            case CT_STRING:
                if (!*item) {
                    if (!emptystr) {
                        emptystr = item;
                        emptystroff = off;
                        emptystrflag = val;
                    }
                } else if ((e = match(item, s)) != 0) {
                    if (ps->flags & val)
                        return E_DUPLICATE_FLAGS;
                    s = e;
                    if (!*skipb(s))
                        return E_MISSING_STRING_ARGUMENT;
                    r = token(&s, &tok);
                    if (!r)
                        r = storestr(ps, &ps->blk[off / 4], tok);
                    if (r)
                        return r;
                    ps->flags |= val;
                    done = 1;
                }
                off += 4;
                break;
            case CT_RECT:
                if (*item && (e = match(item, s)) != 0) {
                    int k;
                    WORD FAR *w = (WORD FAR *)&ps->blk[off / 4];
                    if (ps->flags & val)
                        return E_DUPLICATE_FLAGS;
                    s = e;
                    for (k = 0; k < 4; k++) {
                        if (!*skipb(s))
                            return E_BAD_INTEGER;
                        r = token(&s, &tok);
                        if (r)
                            return r;
                        if (!number(tok, &v))
                            return E_BAD_INTEGER;
                        w[k] = LOWORD(v);
                    }
                    ps->flags |= val;
                    done = 1;
                }
                off += 8;
                break;
            case CT_CONST: {
                LPCSTR q = p;
                if (*item) {
                    if ((e = match(item, s)) != 0) {
                        if (ps->flags & val)
                            return E_DUPLICATE_FLAGS;
                        s = e;
                        r = constval(q, &s, &v);
                        if (r)
                            return r;
                        ps->blk[off / 4] = v;
                        ps->flags |= val;
                        done = 1;
                    }
                } else {
                    /* the values are keywords themselves */
                    DWORD cv;
                    WORD ct;
                    if (!emptyconst) {
                        emptyconst = q;
                        emptyconstoff = off;
                        emptyconstflag = val;
                    }
                    for (;;) {
                        LPCSTR ci = q;
                        q = nextitem(q, &cv, &ct);
                        if (ct == CT_ENDCONST)
                            break;
                        if (*ci && (e = match(ci, s)) != 0) {
                            if (ps->flags & val)
                                return E_FLAGS_NOT_COMPATIBLE;
                            ps->blk[off / 4] = cv;
                            ps->flags |= val;
                            s = e;
                            done = 1;
                            break;
                        }
                    }
                }
                /* past the constant's values */
                for (;;) {
                    p = nextitem(p, &v, &type);
                    if (type == CT_ENDCONST)
                        break;
                }
                off += 4;
                break;
            }
            }
            if (done)
                break;
        }
        if (done)
            continue;
        /* no keyword: a string without one, then a constant's value */
        if (emptystr) {
            if (ps->flags & emptystrflag)
                return E_DUPLICATE_FLAGS;
            r = token(&s, &tok);
            if (!r)
                r = storestr(ps, &ps->blk[emptystroff / 4], tok);
            if (r)
                return r;
            ps->flags |= emptystrflag;
            continue;
        }
        if (emptyconst) {
            r = token(&s, &tok);
            if (r)
                return r;
            if (!number(tok, &v))
                return E_BAD_CONSTANT;
            if (ps->flags & emptyconstflag)
                return E_FLAGS_NOT_COMPATIBLE;
            ps->blk[emptyconstoff / 4] = v;
            ps->flags |= emptyconstflag;
            continue;
        }
        return E_UNRECOGNIZED_KEYWORD;
    }
}

/* the return type of an entry: 0, CT_INTEGER, CT_STRING or CT_RECT */
static WORD rettype(LPCSTR entry)
{
    DWORD v;
    WORD type;
    LPCSTR p = nextitem(entry, 0, 0);
    nextitem(p, &v, &type);
    return type == CT_RETURN ? (WORD)v : 0;
}

static WORD entrymsg(LPCSTR entry)
{
    DWORD v;
    nextitem(entry, &v, 0);
    return LOWORD(v);
}

static int hasnotify(LPSTR args)
{
    LPSTR s = args;
    while (*(s = skipb(s))) {
        if (match("notify", s))
            return 1;
        while (*s && !blank(*s))
            s++;
    }
    return 0;
}

/* ---- [mci] and [mci extensions] ---- */
/* the [mci] key that names a device, as typed: the key itself, or for a
   type's name the type's first key (unnumbered, else the lowest number) */
static int mcikey(LPCSTR name, LPSTR key, int len)
{
    static char keys[1024];
    char *k, *best = 0;
    int n = GetPrivateProfileString("mci", NULL, "", keys, sizeof keys, "SYSTEM.INI");
    int t, bestnum = 0x7FFF;
    keys[n] = 0;
    for (k = keys; *k; k += lstrlen(k) + 1)
        if (!lstrcmpi(k, name)) {
            lstrcpyn(key, k, len);
            return 1;
        }
    for (t = 0; t <= MCI_DEVTYPE_LAST - MCI_DEVTYPE_FIRST; t++)
        if (!lstrcmpi(name, typenames[t]))
            break;
    if (t > MCI_DEVTYPE_LAST - MCI_DEVTYPE_FIRST)
        return 0;
    for (k = keys; *k; k += lstrlen(k) + 1) {
        int l = lstrlen(typenames[t]), num = 0, i;
        char c[24];
        if (lstrlen(k) < l || lstrlen(k) > l + 4)
            continue;
        lstrcpyn(c, k, l + 1);
        if (lstrcmpi(c, typenames[t]))
            continue;
        for (i = l; k[i] >= '0' && k[i] <= '9'; i++)
            num = num * 10 + k[i] - '0';
        if (k[i])
            continue;
        if (num < bestnum) {
            bestnum = num;
            best = k;
        }
    }
    if (!best)
        return 0;
    lstrcpyn(key, best, len);
    return 1;
}

/* the device type an [mci] key's name stands for (0: none) */
static WORD keytype(LPCSTR key)
{
    int t;
    for (t = 0; t <= MCI_DEVTYPE_LAST - MCI_DEVTYPE_FIRST; t++) {
        int l = lstrlen(typenames[t]), i;
        char c[24];
        if (lstrlen(key) < l)
            continue;
        lstrcpyn(c, key, l + 1);
        if (lstrcmpi(c, typenames[t]))
            continue;
        for (i = l; key[i] >= '0' && key[i] <= '9'; i++)
            ;
        if (!key[i])
            return MCI_DEVTYPE_FIRST + t;
    }
    return 0;
}

static int extdevice(LPCSTR element, LPSTR dev, int len)
{
    LPCSTR e = 0, p;
    for (p = element; *p; p++)
        if (*p == '.')
            e = p + 1;
        else if (*p == '\\' || *p == ':')
            e = 0;
    if (!e)
        return 0;
    GetProfileString("mci extensions", e, "", dev, len);
    return dev[0] != 0;
}

/* ---- opening ---- */
struct opendrv {                /* MCI_OPEN_DRIVER_PARMS */
    WORD wDeviceID;
    LPCSTR lpstrParams;
    WORD wCustomCommandTable;
    WORD wType;
};

struct openparms {              /* MCI_OPEN_PARMS */
    DWORD dwCallback;
    WORD wDeviceID;
    WORD wReserved0;
    LPCSTR lpstrDeviceType;
    LPCSTR lpstrElementName;
    LPCSTR lpstrAlias;
};

static struct mcidev *byalias(LPCSTR alias)
{
    HTASK t = GetCurrentTask();
    int i;
    for (i = 1; i <= NDEV; i++)
        if (devs[i] && devs[i]->task == t && !lstrcmpi(devs[i]->alias, alias))
            return devs[i];
    return 0;
}

static UINT idof(struct mcidev *d)
{
    int i;
    for (i = 1; i <= NDEV; i++)
        if (devs[i] == d)
            return i;
    return 0;
}

static void untypetable(UINT t);

static void freedev(UINT id)
{
    struct mcidev *d = getdev(id);
    if (!d)
        return;
    if (d->typetab != MCI_NO_COMMAND_TABLE)
        untypetable(d->typetab);
    devs[id] = 0;
    LocalFree((HLOCAL)(WORD)(void NEAR *)d);
}

/* loads the driver of [mci] key into a new device; *pid its ID */
/* the options handed to a driver: 3.1 copies a line's options into a
   buffer of its own and hands an empty string for a line without any;
   opened by its file name a driver gets the options of the first [mci]
   line that names its file with options (MMP: "mct.drv" got those of
   "mctc=mct.drv table=mcttab", not of "mct=mct.drv" before it nor of a
   line opened since; UNVERIFIED beyond that) */
static char optbuf[100];

static void fileopts(LPCSTR file)
{
    static char keys[1024];
    char v[160], *k;
    int n = GetPrivateProfileString("mci", NULL, "", keys, sizeof keys, "SYSTEM.INI"), l;
    keys[n] = 0;
    l = lstrlen(file);
    for (k = keys; *k; k += lstrlen(k) + 1) {
        int i;
        GetPrivateProfileString("mci", k, "", v, sizeof v, "SYSTEM.INI");
        for (i = 0; i < l && lower(v[i]) == lower(file[i]); i++)
            ;
        if (i == l && blank(v[l])) {
            for (i = l; blank(v[i]); i++)
                ;
            if (v[i]) {
                lstrcpyn(optbuf, v + i, sizeof optbuf);
                return;
            }
        }
    }
    optbuf[0] = 0;
}

static DWORD loaddriver(LPCSTR key, LPCSTR alias, UINT *pid)
{
    char line[160], file[100];
    int byfile = 0;
    struct opendrv o;
    struct mcidev *d;
    UINT id;
    int i;
    char *opts;
    for (id = 1; id <= NDEV && devs[id]; id++)
        ;
    if (id > NDEV)
        return E_OUT_OF_MEMORY;
    if (!GetPrivateProfileString("mci", key, "", line, sizeof line, "SYSTEM.INI")) {
        /* a driver's file name itself (the Guide, ch. 2) */
        lstrcpyn(line, key, sizeof line);
        byfile = 1;
    }
    for (i = 0; line[i] && !blank(line[i]) && i < (int)sizeof file - 1; i++)
        file[i] = line[i];
    file[i] = 0;
    opts = line + i;
    while (blank(*opts))
        opts++;
    d = (struct mcidev *)(void NEAR *)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof *d);
    if (!d)
        return E_OUT_OF_MEMORY;
    d->task = GetCurrentTask();
    d->custom = d->typetab = MCI_NO_COMMAND_TABLE;
    d->yproc = (YIELDPROC)mm_mciyield;
    d->ydata = VK_CANCEL;
    lstrcpyn(d->name, key, sizeof d->name);
    lstrcpyn(d->alias, alias, sizeof d->alias);
    devs[id] = d;
    o.wDeviceID = id;
    if (byfile) {
        fileopts(file);
        o.lpstrParams = optbuf;
    }
    else if (*opts) {
        lstrcpyn(optbuf, opts, sizeof optbuf);
        o.lpstrParams = optbuf;
    } else
        o.lpstrParams = "";
    o.wCustomCommandTable = MCI_NO_COMMAND_TABLE;
    o.wType = 0;
    d->hdrv = OpenDriver(file, NULL, (LPARAM)(LPVOID)&o);
    if (!d->hdrv) {
        freedev(id);
        /* UNVERIFIED for an [mci] line whose driver is missing */
        return byfile ? E_INVALID_DEVICE_NAME : E_CANNOT_LOAD_DRIVER;
    }
    d->type = o.wType;
    d->custom = o.wCustomCommandTable;
    /* the type's table: by the [mci] key's name when it names a type
       ("videodisc", "animation1"), else by the type the driver reported
       (MMP: MCT under "videodisc" reporting "other" got videodisc's) */
    d->typetab = typetable(keytype(key) ? keytype(key) : o.wType);
    *pid = id;
    return 0;
}

static void unload(UINT id)
{
    struct mcidev *d = getdev(id);
    if (!d)
        return;
    if (d->hdrv)
        CloseDriver(d->hdrv, 0, 0);
    freedev(id);
}

static DWORD send(struct mcidev *d, UINT msg, DWORD flags, DWORD parms)
{
    return SendDriverMessage(d->hdrv, msg, flags, parms);
}

/* MCI_OPEN by command (mciSendCommand): the application's block, patched
   for the call as 3.1 does (an alias added, a type ID made a name) */
static DWORD opencmd(DWORD flags, struct openparms FAR *op, DWORD FAR *retflags)
{
    char key[80], dev[80], tname[24];
    LPCSTR type = 0, element = 0, alias = 0, savetype = op->lpstrDeviceType;
    LPCSTR savealias = op->lpstrAlias;
    UINT id;
    DWORD r, f = flags;
    (void)retflags;
    if (!(flags & (MCI_OPEN_TYPE | MCI_OPEN_ELEMENT | MCI_OPEN_ELEMENT_ID)))
        return E_MISSING_PARAMETER;
    if (flags & MCI_OPEN_TYPE) {
        if (flags & MCI_OPEN_TYPE_ID) {
            WORD t = LOWORD((DWORD)op->lpstrDeviceType), n = HIWORD((DWORD)op->lpstrDeviceType);
            if (t < MCI_DEVTYPE_FIRST || t > MCI_DEVTYPE_LAST)
                return E_INVALID_DEVICE_NAME;
            lstrcpy(tname, typenames[t - MCI_DEVTYPE_FIRST]);
            if (n)
                wsprintf(tname + lstrlen(tname), "%u", n);
            if (n ? !GetPrivateProfileString("mci", tname, "", key, sizeof key, "SYSTEM.INI")
                  : !mcikey(tname, key, sizeof key))
                return E_INVALID_DEVICE_NAME;
            if (n)
                lstrcpy(key, tname);    /* the key "other2" itself */
            else
                lstrcpy(tname, key);
            type = tname;
        } else
            type = op->lpstrDeviceType;
    }
    if ((flags & MCI_OPEN_ELEMENT) && !(flags & MCI_OPEN_ELEMENT_ID))
        element = op->lpstrElementName;
    if (!type) {
        if (!element || !extdevice(element, dev, sizeof dev))
            return E_INVALID_DEVICE_NAME;
        type = dev;
        f |= MCI_OPEN_TYPE;
    }
    /* no alias is made up for an element or a type given by number */
    if (flags & MCI_OPEN_ALIAS)
        alias = op->lpstrAlias;
    else if (flags & MCI_OPEN_ELEMENT_ID)
        alias = 0;
    else if (element)
        alias = element;
    else if (!(flags & MCI_OPEN_TYPE_ID))
        alias = type;
    if (alias && byalias(alias))
        return flags & MCI_OPEN_ALIAS ? E_DUPLICATE_ALIAS : E_DEVICE_OPEN;
    if (!(flags & MCI_OPEN_TYPE_ID) && !mcikey(type, key, sizeof key)) {
        /* a driver's file in the system directory (the Guide, ch. 2): no
           path (3.1: 263) */
        if (!haschar(type, '.') || haschar(type, '\\') || haschar(type, ':'))
            return E_INVALID_DEVICE_NAME;
        lstrcpyn(key, type, sizeof key);
    }
    r = loaddriver(key, alias ? alias : "", &id);
    if (r)
        return r;
    if (flags & MCI_OPEN_ELEMENT_ID)
        devs[id]->elementid = (DWORD)op->lpstrElementName;
    op->wDeviceID = id;
    if (alias && !(flags & MCI_OPEN_ALIAS)) {
        op->lpstrAlias = alias;
        f |= MCI_OPEN_ALIAS;
    }
    if (flags & MCI_OPEN_TYPE_ID)
        op->lpstrDeviceType = type;
    else if (!(flags & MCI_OPEN_TYPE))
        op->lpstrDeviceType = type;
    r = send(devs[id], MCI_OPEN_DRIVER, f, (DWORD)op);
    op->lpstrDeviceType = savetype;
    op->lpstrAlias = savealias;
    if (LOWORD(r)) {
        unload(id);
        return r;
    }
    return 0;
}

/* ---- the commands MMSYSTEM carries out itself ---- */
struct sysinfoparms {           /* MCI_SYSINFO_PARMS */
    DWORD dwCallback;
    LPSTR lpstrReturn;
    DWORD dwRetSize;
    DWORD dwNumber;
    WORD wDeviceType;
    WORD wReserved0;
};

static void notify(DWORD flags, DWORD cb, UINT dev)
{
    if ((flags & MCI_NOTIFY) && LOWORD(cb))
        PostMessage((HWND)LOWORD(cb), MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MAKELONG(dev, 0));
}

/* MCI_SYSINFO (the Guide, ch. 2; MMP): answers MCI_INTEGER_RETURNED for a
   quantity, written as a DWORD into the return buffer */
static DWORD sysinfo(UINT dev, DWORD flags, struct sysinfoparms FAR *sp)
{
    static char keys[1024];
    struct mcidev *d;
    DWORD n = 0, want;
    int i, all = dev == MCI_ALL_DEVICE_ID;
    char *k;
    LPCSTR name = 0;
    if (!sp)
        return E_NULL_PARAMETER_BLOCK;
    if (flags & MCI_SYSINFO_INSTALLNAME) {
        if (all)
            return E_CANNOT_USE_ALL;
        d = getdev(dev);
        if (!d)
            return dev ? E_INVALID_DEVICE_ID : E_INVALID_DEVICE_NAME;
        if (!sp->lpstrReturn || sp->dwRetSize < (DWORD)lstrlen(d->name) + 1)
            return E_PARAM_OVERFLOW;
        lstrcpy(sp->lpstrReturn, d->name);
        notify(flags, sp->dwCallback, 0);
        return 0;
    }
    if ((flags & MCI_SYSINFO_QUANTITY) && (flags & MCI_SYSINFO_NAME))
        return E_FLAGS_NOT_COMPATIBLE;
    if (!(flags & (MCI_SYSINFO_QUANTITY | MCI_SYSINFO_NAME)))
        return E_MISSING_PARAMETER;
    if (!all && !sp->wDeviceType)
        return E_DEVICE_TYPE_REQUIRED;
    if (!sp->lpstrReturn || !sp->dwRetSize)
        return E_PARAM_OVERFLOW;
    want = flags & MCI_SYSINFO_NAME ? sp->dwNumber : 0;
    if (flags & MCI_SYSINFO_OPEN) {
        HTASK t = GetCurrentTask();
        for (i = 1; i <= NDEV; i++) {
            d = devs[i];
            if (d && d->task == t && (all || d->type == sp->wDeviceType))
                if (++n == want)
                    name = d->alias;
        }
    } else {
        int len = GetPrivateProfileString("mci", NULL, "", keys, sizeof keys, "SYSTEM.INI");
        keys[len] = 0;
        for (k = keys; *k; k += lstrlen(k) + 1)
            if (all || keytype(k) == sp->wDeviceType)
                if (++n == want)
                    name = k;
    }
    if (flags & MCI_SYSINFO_QUANTITY) {
        *(DWORD FAR *)sp->lpstrReturn = n;
        notify(flags, sp->dwCallback, 0);
        return MCI_INTEGER_RETURNED;
    }
    if (!name)
        return E_OUTOFRANGE;
    lstrcpy(sp->lpstrReturn, name);     /* 3.1 does not look at the size */
    notify(flags, sp->dwCallback, 0);
    return 0;
}

/* MCI_BREAK: the default yield procedure with the key (MMP) */
static DWORD dobreak(struct mcidev *d, DWORD flags, DWORD FAR *bp)
{
    if (flags & MCI_BREAK_KEY) {
        if (!bp)
            return E_NULL_PARAMETER_BLOCK;
        d->yproc = (YIELDPROC)mm_mciyield;
        d->ydata = LOWORD(bp[1]);
        d->breakwnd = flags & MCI_BREAK_HWND ? (HWND)LOWORD(bp[2]) : 0;
        d->breakoff = 0;
        return 0;
    }
    if (flags & MCI_BREAK_OFF) {
        d->breakoff = 1;
        return 0;
    }
    return E_MISSING_PARAMETER;
}

/* MCI_SOUND: sndPlaySound with the name, or the default sound;
   UNVERIFIED: its flags (only failures, without a wave device, seen) */
static DWORD sound(DWORD flags, DWORD FAR *sp)
{
    LPCSTR name = (flags & MCI_SOUND_NAME) && sp ? (LPCSTR)sp[1] : "SystemDefault";
    return SNDPLAYSOUND(name, 0x0001) ? 0 : E_HARDWARE;
}

/* a command to one device, the driver's answer whole (mciSendString needs
   its high word) */
static DWORD command(UINT dev, UINT msg, DWORD flags, DWORD parms)
{
    struct mcidev *d;
    DWORD r;
    if (msg == MCI_OPEN) {
        if (!parms)
            return E_NULL_PARAMETER_BLOCK;
        return opencmd(flags, (struct openparms FAR *)parms, 0);
    }
    if (msg == MCI_SYSINFO)
        return sysinfo(dev, flags, (struct sysinfoparms FAR *)parms);
    if (msg == MCI_SOUND)
        return sound(flags, (DWORD FAR *)parms);
    if (dev == MCI_ALL_DEVICE_ID) {
        /* each of the task's devices in turn; UNVERIFIED: what 3.1 answers
           when several fail (MCIERR_MULTIPLE assumed) */
        HTASK t = GetCurrentTask();
        DWORD last = 0;
        int i, nerr = 0;
        for (i = 1; i <= NDEV; i++)
            if (devs[i] && devs[i]->task == t) {
                r = command(i, msg, flags, parms);
                if (LOWORD(r)) {
                    nerr++;
                    last = r;
                }
            }
        return nerr > 1 ? E_MULTIPLE : last;
    }
    d = getdev(dev);
    if (!d)
        return E_INVALID_DEVICE_ID;
    if (msg == MCI_BREAK)
        return dobreak(d, flags, (DWORD FAR *)parms);
    if (msg == MCI_CLOSE) {
        r = send(d, MCI_CLOSE_DRIVER, flags, parms);
        unload(dev);
        return r;
    }
    return send(d, msg, flags, parms);
}

/* an error as the application gets it: the low word, and for a driver's
   own errors (512 and up) the device's ID in the high word */
static DWORD errout(DWORD r, UINT dev)
{
    UINT e = LOWORD(r);
    if (e >= E_CUSTOM_DRIVER_BASE && dev && dev != MCI_ALL_DEVICE_ID)
        return MAKELONG(e, dev);
    return e;
}

DWORD MMAPI MCISENDCOMMAND(UINT dev, UINT msg, DWORD flags, DWORD parms)
{
    DWORD r = command(dev, msg, flags, parms);
    return LOWORD(r) ? errout(r, dev) : 0;
}

/* ---- the command-string interface ---- */
#define BLKSIZE 128

/* the device token of a command string (mciExecute's caption) */
static void devtoken(LPCSTR cmd, LPSTR out, int len)
{
    LPCSTR s = cmd;
    int i = 0;
    while (blank(*s))
        s++;
    while (*s && !blank(*s))
        s++;
    while (blank(*s))
        s++;
    if (*s == '"') {
        s++;
        while (*s && *s != '"' && i < len - 1)
            out[i++] = *s++;
    } else
        while (*s && !blank(*s) && i < len - 1)
            out[i++] = *s++;
    out[i] = 0;
}

/* "open": the device, element and alias, the driver loaded, the
   arguments parsed with its own "open" (the quirk) or the core's, then
   MCI_OPEN_DRIVER */
static DWORD stropen(LPSTR dtok, LPSTR args, HWND hwnd, UINT *pid)
{
    static char ext[80], key[80];
    char scan[256], *s, *x = 0, *y = 0, *t;
    LPSTR type = 0, element = 0, alias, bang;
    struct parse ps;
    struct mcidev *d;
    LPCSTR own;
    UINT id;
    DWORD r, f;
    int isnew = 0, i;
    HLOCAL hb;
    /* the type and alias keywords, looked for before the driver is
       loaded */
    lstrcpyn(scan, args, sizeof scan);
    for (s = scan; *(s = (char *)skipb(s));) {
        if (token((LPSTR FAR *)&s, (LPSTR FAR *)&t))
            break;
        if (!lstrcmpi(t, "type") || !lstrcmpi(t, "alias")) {
            char *v;
            if (token((LPSTR FAR *)&s, (LPSTR FAR *)&v))
                break;
            if (lower(t[0]) == 't')
                x = v;
            else
                y = v;
        }
    }
    for (bang = dtok; *bang && *bang != '!'; bang++)
        ;
    if (!lstrcmpi(dtok, "new")) {
        isnew = 1;
    } else if (*bang) {
        *bang = 0;
        type = dtok;
        element = bang + 1;
    } else if (x) {
        type = x;
        element = dtok;
    } else if (haschar(dtok, '.')) {
        element = dtok;
        if (!extdevice(element, ext, sizeof ext))
            return E_INVALID_DEVICE_NAME;
        type = ext;
    } else
        type = dtok;
    if (isnew) {
        if (!y)
            return E_NEW_REQUIRES_ALIAS;
        if (!x)
            return E_INVALID_DEVICE_NAME;
        type = x;
        element = "";
    }
    alias = y ? y : element ? element : type;
    if (byalias(alias))
        return y ? E_DUPLICATE_ALIAS : E_DEVICE_OPEN;
    if (!mcikey(type, key, sizeof key)) {
        if (!haschar(type, '.') || haschar(type, '\\') || haschar(type, ':'))
            return E_INVALID_DEVICE_NAME;
        lstrcpyn(key, type, sizeof key);
    }
    r = loaddriver(key, alias, &id);
    if (r)
        return r;
    d = devs[id];
    hb = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, BLKSIZE);
    if (!hb) {
        unload(id);
        return E_OUT_OF_MEMORY;
    }
    ps.blk = (LPDWORD)(DWORD NEAR *)hb;
    ps.flags = 0;
    ps.ncopies = 0;
    own = findverb(d->custom, "open");
    if (!own && d->typetab != MCI_NO_COMMAND_TABLE)
        own = findverb(d->typetab, "open");
    if (own) {
        /* 3.1: a fresh block, no device ID, the window only with notify,
           every string 4 bytes into a copy (storestr) */
        ps.quirk = 1;
        r = parseargs(own, args, &ps);
        if (!r && (ps.flags & MCI_NOTIFY))
            ps.blk[0] = (DWORD)(WORD)hwnd;
    } else {
        int core;
        ps.quirk = 0;
        r = parseargs(devverb(0, "open", &core), args, &ps);
        ps.blk[0] = (DWORD)(WORD)hwnd;
        ps.blk[1] = id;
    }
    f = ps.flags;
    if (!r) {
        if (!(f & MCI_OPEN_TYPE))
            ps.blk[2] = (DWORD)type;
        f |= MCI_OPEN_TYPE;
        if (element) {
            ps.blk[3] = (DWORD)element;
            f |= MCI_OPEN_ELEMENT;
        }
        if (!(f & MCI_OPEN_ALIAS))
            ps.blk[4] = (DWORD)alias;
        f |= MCI_OPEN_ALIAS;
        r = send(d, MCI_OPEN_DRIVER, f, (DWORD)ps.blk);
    }
    for (i = 0; i < ps.ncopies; i++)
        LocalFree(ps.copies[i]);
    LocalFree(hb);
    if (LOWORD(r)) {
        unload(id);
        return r;
    }
    *pid = id;
    return 0;
}

/* the answer as a string (MMP's returns sections) */
static DWORD convert(DWORD r, WORD rt, LPDWORD blk, LPSTR ret, UINT size, struct mcidev *d,
                     DWORD value)
{
    char tmp[40];
    if (!ret || !size)                  /* 3.1: size 0 is no buffer */
        return 0;
    if (rt == CT_INTEGER) {
        if (r & MCI_RESOURCE_RETURNED) {
            HINSTANCE inst = (r & MCI_RESOURCE_DRIVER) && d ?
                (HINSTANCE)GetDriverModuleHandle(d->hdrv) : mm_hinst;
            if (!size || !LoadString(inst, HIWORD(value), ret, size))
                if (size)
                    *ret = 0;
            return 0;
        }
        if (size < 12)
            return E_PARAM_OVERFLOW;
        if (r & (MCI_COLONIZED3_RETURN | MCI_COLONIZED4_RETURN)) {
            BYTE b0 = LOBYTE(LOWORD(value)), b1 = HIBYTE(LOWORD(value));
            BYTE b2 = LOBYTE(HIWORD(value)), b3 = HIBYTE(HIWORD(value));
            if ((r & MCI_COLONIZED3_RETURN) && (r & MCI_COLONIZED4_RETURN))
                wsprintf(ret, "%02d:%02d:%02d:", b0, b1, b2);
            else if (r & MCI_COLONIZED3_RETURN)
                wsprintf(ret, "%02d:%02d:%02d", b0, b1, b2);
            else
                wsprintf(ret, "%02d:%02d:%02d:%02d", b0, b1, b2, b3);
            return 0;
        }
        if (r & MCI_INTEGER_RETURNED) {
            /* 3.1 reads the DWORD the value points to (meant for string
               returns); UNVERIFIED for an integer: a bad pointer gives 0 */
            DWORD FAR *pv = (DWORD FAR *)value;
            value = !IsBadReadPtr(pv, 4) ? *pv : 0;
        }
        wsprintf(ret, "%ld", value);
        return 0;
    }
    if (rt == CT_STRING) {
        if (r & MCI_INTEGER_RETURNED) {
            if (size < 12)
                return E_PARAM_OVERFLOW;
            wsprintf(ret, "%ld", *(DWORD FAR *)ret);
        }
        return 0;
    }
    if (rt == CT_RECT) {
        WORD FAR *w = (WORD FAR *)&blk[1];
        wsprintf(tmp, "%d %d %d %d", (int)w[0], (int)w[1], (int)w[2], (int)w[3]);
        if (size)
            lstrcpyn(ret, tmp, size);
        return 0;
    }
    (void)blk;
    return 0;
}

/* the string interface; hwnd the callback window (the auto-open's own:
   MMSYSTEM's) */
static DWORD sendstring(LPCSTR cmd, LPSTR ret, UINT size, HWND hwnd)
{
    HLOCAL hc, hb;
    LPSTR s, verb, dtok;
    DWORD r = 0, value;
    struct parse ps;
    struct mcidev *d = 0;
    UINT id = 0;
    LPCSTR entry;
    WORD rt;
    int core, autoopen = 0;
    if (!cmd)
        return E_MISSING_COMMAND_STRING;
    hc = LocalAlloc(LMEM_FIXED, lstrlen(cmd) + 2);
    hb = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, BLKSIZE);
    if (!hc || !hb) {
        if (hc)
            LocalFree(hc);
        if (hb)
            LocalFree(hb);
        return E_OUT_OF_MEMORY;
    }
    s = (LPSTR)(char NEAR *)hc;
    lstrcpy(s, cmd);
    s[lstrlen(s) + 1] = 0;
    ps.blk = (LPDWORD)(DWORD NEAR *)hb;
    ps.flags = 0;
    ps.quirk = 0;
    ps.ncopies = 0;
    r = token(&s, &verb);
    if (!r && !*verb)
        r = E_MISSING_COMMAND_STRING;
    if (!r && !*skipb(s))
        r = E_MISSING_DEVICE_NAME;
    if (!r)
        r = token(&s, &dtok);
    if (r)
        goto out;
    if (sameword(verb, "open")) {
        r = stropen(dtok, s, hwnd, &id);
        if (!r) {
            r = convert(0, CT_INTEGER, ps.blk, ret, size, devs[id], id);
            /* the device stays open whatever the buffer */
        }
        goto out;
    }
    if (sameword(verb, "sysinfo")) {
        int t;
        struct sysinfoparms FAR *sp = (struct sysinfoparms FAR *)ps.blk;
        entry = devverb(0, "sysinfo", &core);
        if (sameword(dtok, "all"))
            id = MCI_ALL_DEVICE_ID;
        else {
            for (t = 0; t <= MCI_DEVTYPE_LAST - MCI_DEVTYPE_FIRST; t++)
                if (sameword(dtok, typenames[t]))
                    break;
            if (t <= MCI_DEVTYPE_LAST - MCI_DEVTYPE_FIRST)
                sp->wDeviceType = MCI_DEVTYPE_FIRST + t;
            else if ((d = byalias(dtok)) != 0)
                id = idof(d);
            else {
                r = E_DEVICE_TYPE_REQUIRED;
                goto out;
            }
        }
        sp->lpstrReturn = ret;
        sp->dwRetSize = ret ? size : 0;
        r = parseargs(entry, s, &ps);
        if (r)
            goto out;
        sp->dwCallback = (DWORD)(WORD)hwnd;
        if ((ps.flags & MCI_SYSINFO_INSTALLNAME) && !id && !d) {
            r = E_INVALID_DEVICE_NAME;
            goto out;
        }
        r = sysinfo(id, ps.flags, sp);
        if (!LOWORD(r))
            r = convert(r, CT_STRING, ps.blk, ret, size, d, 0);
        goto out;
    }
    if (sameword(verb, "sound")) {
        entry = devverb(0, "sound", &core);
        r = parseargs(entry, s, &ps);
        if (r)
            goto out;
        ps.blk[0] = (DWORD)(WORD)hwnd;
        ps.blk[1] = (DWORD)dtok;
        r = sound(ps.flags | MCI_SOUND_NAME, ps.blk);
        goto out;
    }
    if (sameword(dtok, "all")) {
        id = MCI_ALL_DEVICE_ID;
        entry = devverb(0, verb, &core);
        if (!entry) {
            r = E_UNRECOGNIZED_COMMAND;
            goto out;
        }
        if (rettype(entry)) {
            r = E_CANNOT_USE_ALL;
            goto out;
        }
    } else {
        d = byalias(dtok);
        if (!d) {
            if (sameword(verb, "close")) {
                r = E_INVALID_DEVICE_NAME;
                goto out;
            }
            /* auto-open: opened with MMSYSTEM's window as the callback,
               the command sent with notify to it, closed when it is
               notified (snd.c, mci_notified) */
            if (hasnotify(s)) {
                r = E_NOTIFY_ON_AUTO_OPEN;
                goto out;
            }
            {
                char dcopy[128];
                lstrcpyn(dcopy, dtok, sizeof dcopy);
                r = stropen(dcopy, "", snd_window(), &id);
            }
            if (r)
                goto out;
            d = devs[id];
            d->autoopen = 1;
            autoopen = 1;
            hwnd = snd_window();
        } else
            id = idof(d);
        entry = devverb(d, verb, &core);
    }
    if (!entry) {
        r = E_UNRECOGNIZED_COMMAND;
        goto closeauto;
    }
    rt = rettype(entry);
    ps.blk[0] = (DWORD)(WORD)hwnd;
    if (rt == CT_STRING) {
        ps.blk[1] = (DWORD)ret;
        ps.blk[2] = ret ? size : 0;
    }
    r = parseargs(entry, s, &ps);
    if (r)
        goto closeauto;
    if (autoopen)
        ps.flags |= MCI_NOTIFY;
    r = command(id, entrymsg(entry), ps.flags, (DWORD)ps.blk);
    if (LOWORD(r)) {
        r = errout(r, id);
        goto out;
    }
    value = rt == CT_INTEGER ? ps.blk[1] : 0;
    r = convert(r, rt, ps.blk, ret, size, getdev(id), value);
    goto out;
closeauto:
    if (autoopen)
        command(id, MCI_CLOSE, 0, 0);
out:
    LocalFree(hb);
    LocalFree(hc);
    return r;
}

DWORD MMAPI MCISENDSTRING(LPCSTR cmd, LPSTR ret, UINT size, HWND hwnd)
{
    if (ret)
        *ret = 0;
    return sendstring(cmd, ret, size, hwnd);
}

/* MMSYSTEM's window got MM_MCINOTIFY: an auto-opened device has finished
   its command (snd.c) */
void mci_notified(UINT dev)
{
    struct mcidev *d = getdev(dev);
    DWORD blk[8];                       /* 3.1's: a block on the stack */
    if (d && d->autoopen) {
        blk[0] = 0;
        command(dev, MCI_CLOSE, 0, (DWORD)(LPVOID)blk);
    }
}

/* a task's end (MMSYSTEM's DRV_EXITAPPLICATION, core.c): its devices are
   closed. UNVERIFIED on 3.1 */
void mci_taskexit(void)
{
    HTASK t = GetCurrentTask();
    int i;
    for (i = 1; i <= NDEV; i++)
        if (devs[i] && devs[i]->task == t)
            command(i, MCI_CLOSE, 0, 0);
}

/* ---- the other exports ---- */
UINT MMAPI MCIGETDEVICEID(LPCSTR name)
{
    struct mcidev *d;
    if (!name || !*name)
        return 0;
    if (!lstrcmpi(name, "all"))
        return MCI_ALL_DEVICE_ID;
    d = byalias(name);
    return d ? idof(d) : 0;
}

UINT MMAPI MCIGETDEVICEIDFROMELEMENTID(DWORD element, LPCSTR type)
{
    int i;
    for (i = 1; i <= NDEV; i++)
        if (devs[i] && devs[i]->elementid == element && devs[i]->elementid &&
            (!type || !lstrcmpi(devs[i]->name, type)))
            return i;
    return 0;
}

/* the error's text: MMSYSTEM's string n for error n; a driver's own error
   (512 and up, the device's ID in the high word) from the driver's
   module; that device no longer open: 278's text (MMP) */
BOOL MMAPI MCIGETERRORSTRING(DWORD err, LPSTR buf, UINT size)
{
    HINSTANCE inst = mm_hinst;
    UINT e = LOWORD(err);
    if (!size || !buf)
        return FALSE;
    if (HIWORD(err)) {
        struct mcidev *d = getdev(HIWORD(err));
        if (e < E_CUSTOM_DRIVER_BASE) {
            *buf = 0;
            return FALSE;
        }
        if (!d)
            e = E_DRIVER_CLOSED;
        else
            inst = (HINSTANCE)GetDriverModuleHandle(d->hdrv);
    }
    if (!LoadString(inst, e, buf, size)) {
        *buf = 0;
        return FALSE;
    }
    return TRUE;
}

BOOL MMAPI MCIEXECUTE(LPCSTR cmd)
{
    char text[256], cap[80];
    DWORD r = sendstring(cmd, NULL, 0, 0);
    if (!r)
        return TRUE;
    MCIGETERRORSTRING(r, text, sizeof text);
    devtoken(cmd ? cmd : "", cap, sizeof cap);
    MessageBox(0, text, cap, MB_OK | MB_ICONHAND);
    return FALSE;
}

BOOL MMAPI MCISETDRIVERDATA(UINT dev, DWORD data)
{
    struct mcidev *d = getdev(dev);
    if (!d)
        return FALSE;
    d->drvdata = data;
    return TRUE;
}

DWORD MMAPI MCIGETDRIVERDATA(UINT dev)
{
    struct mcidev *d = getdev(dev);
    return d ? d->drvdata : 0;
}

HTASK MMAPI MCIGETCREATORTASK(UINT dev)
{
    struct mcidev *d = getdev(dev);
    return d ? d->task : 0;
}

BOOL MMAPI MCISETYIELDPROC(UINT dev, YIELDPROC proc, DWORD data)
{
    struct mcidev *d = getdev(dev);
    if (!d || !proc)
        return FALSE;
    d->yproc = proc;
    d->ydata = data;
    return TRUE;
}

YIELDPROC MMAPI MCIGETYIELDPROC(UINT dev, DWORD FAR *data)
{
    struct mcidev *d = getdev(dev);
    if (!d)
        return 0;
    if (data)
        *data = d->ydata;
    return d->yproc;
}

UINT MMAPI MCIDRIVERYIELD(UINT dev)
{
    struct mcidev *d = getdev(dev);
    if (!d || !d->yproc)
        return 0;
    return d->yproc(dev, d->ydata);
}

/* PostMessage's answer as 3.1's (MMP: a driver's notification) */
BOOL MMAPI MCIDRIVERNOTIFY(HWND hwnd, UINT dev, UINT status)
{
    return PostMessage(hwnd, MM_MCINOTIFY, status, MAKELONG(dev, 0));
}

/* a driver's (or application's) table from its module's RCDATA; 3.1's
   numbers: the lowest free from 1, 0 is the core table (MMP) */
UINT MMAPI MCILOADCOMMANDRESOURCE(HINSTANCE inst, LPCSTR name, UINT type)
{
    HRSRC r;
    HGLOBAL h;
    UINT t;
    if (!inst)
        return MCI_NO_COMMAND_TABLE;
    coretable();
    r = FindResource(inst, name, RT_RCDATA);
    if (!r)
        return MCI_NO_COMMAND_TABLE;
    h = LoadResource(inst, r);
    if (!h)
        return MCI_NO_COMMAND_TABLE;
    t = newtable(h, type, 1);
    if (t == MCI_NO_COMMAND_TABLE)
        FreeResource(h);
    return t;
}

BOOL MMAPI MCIFREECOMMANDRESOURCE(UINT t)
{
    int k;
    if (!t || t >= NTAB || !tabs[t].used)
        return FALSE;
    for (k = 0; k <= MCI_DEVTYPE_LAST - MCI_DEVTYPE_FIRST; k++)
        if (typetabs[k] == t + 1)
            return FALSE;
    UnlockResource(tabs[t].h);
    FreeResource(tabs[t].h);
    tabs[t].used = 0;
    tabs[t].p = 0;
    return TRUE;
}
