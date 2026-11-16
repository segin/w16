/*
 * dm.c - DM.EXE, the DDEML probe (black-box observation of Windows 3.1's
 * DDEML.DLL; ARCHITECTURE.md, chapter "DDEML").
 *
 * One program plays every part: DDEML client, DDEML server, DDEML
 * monitor, raw-DDE client and raw-DDE server. Several copies run at once,
 * each named by a letter: A is the shell (tests/refrun.sh) and runs the
 * script C:\DM.TXT; "start B" starts C:\DM.EXE B ("startx B" starts
 * C:\DMX.EXE B: the same program importing DDEMX, a renamed copy of
 * some DDEML, so that two DDEML libraries talk to each other). A line
 * "B: command" is posted to B, which runs it and answers; A pumps
 * messages meanwhile, so every copy can be client or server.
 *
 * Everything goes to C:\OS.LOG through the DDE spy (OSSPY.DLL,
 * tests/olesvr/osspy.c), which also logs every WM_DDE_* message, so a
 * call, the messages it causes and the callbacks they cause are in one
 * log in the order they happened. Handles are logged by name: string
 * handles by their string, conversations, lists, data handles and
 * instances as c1, l1, d1, i1 (numbered per copy in order of first
 * sight); "raw on" adds the values themselves.
 *
 * Commands (handles: a word or "quoted string" is the string handle of
 * that string, created with DdeCreateStringHandle the first time it is
 * named; 0 is NULL; cN/lN/dN are the named handles):
 *   start X | startx X | quit X | wait MS | settle MS | raw on|off
 *   run CMDLINE | wins | cls | inst N (select DDEML instance slot N)
 *   init [AFCMD-hex] | uninit | lasterr | reinit AFCMD-hex
 *   hsz STR [CP] | freehsz STR | keephsz STR | qstr STR [MAX] | cmp S1 S2
 *   hszval STR (the handle's value against GlobalFindAtom)
 *   ns STR reg|unreg|on|off[|N]       DdeNameService (STR 0: NULL)
 *   conn SVC TOPIC [cc]  connlist SVC TOPIC [lN] [cc]  next lN [cN]
 *   disc cN | disclist lN | reconn cN | info cN [ID] | setuser cN ID V
 *   xact cN TYPE ITEM FMT DATA TIMEOUT|async     TYPE request poke exec
 *        advstart[+nodata][+ackreq] advstop; DATA - | text:STR | dN
 *   abandon cN|0 ID|0 | postadv TOPIC ITEM | enable cN|0 all|one|off|query
 *   mkdata dN TEXT ITEM FMT [own] | adddata dN TEXT OFF | getdata dN [MAX]
 *   access dN | unaccess dN | freedata dN | hdata dN HEX (name a value)
 *   ret TYPE RESPONSE  (callback answers: 0 1 ack busy block nack
 *        text:STR data:dN pairs:SVC/TOPIC,... hex:N; default per type)
 *   cbdo TYPE COMMAND (run COMMAND once inside the next TYPE callback)
 * Raw DDE (a plain window, class DMRaw):
 *   rinit APP TOPIC | rexec CMDS | rreq ITEM FMT | rpoke ITEM FMT TEXT |
 *   radv ITEM FMT FLAGS | runadv ITEM FMT | rterm | rack pos|neg|busy|none
 *   rserve APP TOPIC (answer INITIATEs; * = any) | radvdata ITEM FMT TEXT
 */
#include <windows.h>
#include <dde.h>
#include <ddeml.h>
#include "../olesvr/osspy.h"

/* the OpenWatcom header declares a near result; the SDK has BYTE FAR * */
extern BYTE FAR * FAR PASCAL DdeAccessDataF(HDDEDATA, DWORD FAR *);

#define P OsPrintf

extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define WM_DMCMD  (WM_USER + 100)       /* lParam: atom of the command line */
#define WM_DMDONE (WM_USER + 101)

static HINSTANCE inst;
static HWND me, rawwnd, ctl;            /* ctl: A's window (in B..) */
static char letter = 'A';
static BOOL rawvals, done;
static DWORD settle = 100;

/* ---- small helpers ---- */

static void pump(DWORD ms, BOOL *until)
{
    DWORD end = GetTickCount() + ms;
    MSG m;
    while ((long)(GetTickCount() - end) < 0 && !(until && *until)) {
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        Yield();
    }
}

static BOOL isdig(char c) { return c >= '0' && c <= '9'; }

static DWORD num(const char *s)
{
    DWORD v = 0;
    BOOL hex = FALSE;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        hex = TRUE;
        s += 2;
    }
    for (;; s++) {
        char c = *s;
        if (isdig(c))                   /* no C runtime: no 32-bit multiply */
            v = (hex ? v << 4 : (v << 3) + (v << 1)) + (c - '0');
        else if (hex && c >= 'a' && c <= 'f')
            v = (v << 4) + (c - 'a' + 10);
        else if (hex && c >= 'A' && c <= 'F')
            v = (v << 4) + (c - 'A' + 10);
        else
            break;
    }
    return v;
}

static DWORD hexnum(const char *s)
{
    DWORD v = 0;
    for (;; s++) {
        char c = *s;
        if (isdig(c))
            v = (v << 4) + (c - '0');
        else if (c >= 'a' && c <= 'f')
            v = (v << 4) + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v = (v << 4) + (c - 'A' + 10);
        else
            break;
    }
    return v;
}

/* next token: a word, or "a quoted string" (quotes kept off; a
   returned token that was quoted is marked by *quoted) */
static char *tok(char **s, BOOL *quoted)
{
    char *w;
    if (quoted)
        *quoted = FALSE;
    while (**s == ' ')
        (*s)++;
    if (**s == '"') {
        w = ++*s;
        while (**s && **s != '"')
            (*s)++;
        if (**s)
            *(*s)++ = 0;
        if (quoted)
            *quoted = TRUE;
        return w;
    }
    w = *s;
    while (**s && **s != ' ')
        (*s)++;
    if (**s)
        *(*s)++ = 0;
    return w;
}

static BOOL eq(const char *a, const char *b) { return !lstrcmpi(a, b); }

static BOOL prefix(const char *s, const char *p)
{
    while (*p)
        if (*s++ != *p++)
            return FALSE;
    return TRUE;
}

/* ---- names ---- */

static const char *xtname(UINT t)
{
    static const char *n[] = { "ERROR", "ADVDATA", "ADVREQ", "ADVSTART", "ADVSTOP", "EXECUTE",
                               "CONNECT", "CONNECT_CONFIRM", "XACT_COMPLETE", "POKE",
                               "REGISTER", "REQUEST", "DISCONNECT", "UNREGISTER",
                               "WILDCONNECT", "MONITOR" };
    return n[(t >> 4) & 15];
}

static const char *errname(UINT e)
{
    static const char *n[] = { "ADVACKTIMEOUT", "BUSY", "DATAACKTIMEOUT", "DLL_NOT_INITIALIZED",
                               "DLL_USAGE", "EXECACKTIMEOUT", "INVALIDPARAMETER", "LOW_MEMORY",
                               "MEMORY_ERROR", "NOTPROCESSED", "NO_CONV_ESTABLISHED",
                               "POKEACKTIMEOUT", "POSTMSG_FAILED", "REENTRANCY", "SERVER_DIED",
                               "SYS_ERROR", "UNADVACKTIMEOUT", "UNFOUND_QUEUE_ID" };
    static char b[16];
    if (e == 0)
        return "NO_ERROR";
    if (e >= 0x4000 && e <= 0x4011)
        return n[e - 0x4000];
    wsprintf(b, "%04X", e);
    return b;
}

static char *fmtname(UINT cf)
{
    static char b[64];
    static const char *std[] = { "0", "CF_TEXT", "CF_BITMAP", "CF_METAFILEPICT", "CF_SYLK",
                                 "CF_DIF", "CF_TIFF", "CF_OEMTEXT", "CF_DIB", "CF_PALETTE" };
    if (cf < 10)
        return (char *)std[cf];
    if (GetClipboardFormatName(cf, b + 1, sizeof b - 2)) {
        b[0] = '\'';
        lstrcat(b, "'");
    } else
        wsprintf(b, "cf%04X", cf);
    return b;
}

static UINT parsefmt(const char *s)
{
    static const char *std[] = { "0", "CF_TEXT", "CF_BITMAP", "CF_METAFILEPICT", "CF_SYLK",
                                 "CF_DIF", "CF_TIFF", "CF_OEMTEXT", "CF_DIB", "CF_PALETTE" };
    UINT i;
    if (isdig(*s))
        return (UINT)num(s);
    for (i = 0; i < 10; i++)
        if (eq(s, std[i]))
            return i;
    return RegisterClipboardFormat(s);
}

/* named handles: kind letter + number, numbered in order of first sight */
#define NSYM 512
static struct { DWORD v; char k; WORD n; } sym[NSYM];
static WORD nsym, kn[26];

static char *sname(char k, DWORD v)
{
    static char b[4][32];
    static int r;
    char *o = b[r++ & 3];
    WORD i;
    if (!v) {
        lstrcpy(o, "0");
        return o;
    }
    for (i = 0; i < nsym; i++)
        if (sym[i].k == k && sym[i].v == v)
            break;
    if (i == nsym && nsym < NSYM) {
        sym[i].k = k;
        sym[i].v = v;
        sym[i].n = ++kn[k - 'a'];
        nsym++;
    }
    if (i < nsym)
        wsprintf(o, "%c%u", k, sym[i].n);
    else
        lstrcpy(o, "?");
    if (rawvals)
        wsprintf(o + lstrlen(o), "[%08lX]", v);
    return o;
}

/* a value just made by a call (a data handle): a new name even if the
   value was seen before (libraries reuse their handles differently) */
static char *snew(char k, DWORD v)
{
    WORD i;
    if (v && v != 1 && v != 0xFFFFFFFFL)
        for (i = 0; i < nsym; i++)
            if (sym[i].k == k && sym[i].v == v)
                sym[i].k = '-';
    return sname(k, v);
}

/* give a value the name the script chose (kN), unless it has one */
static void setname(char k, DWORD v, const char *name)
{
    WORD i, n = (WORD)num(name + 1);
    if (*name != k || nsym >= NSYM)
        return;
    for (i = 0; i < nsym; i++)
        if (sym[i].k == k && (sym[i].v == v || sym[i].n == n))
            sym[i].k = '-';
    if (nsym >= NSYM)
        return;
    sym[nsym].k = k;
    sym[nsym].v = v;
    sym[nsym].n = n;
    nsym++;
    if (kn[k - 'a'] < n)
        kn[k - 'a'] = n;
}

static DWORD lastconv;                  /* "c." in a script */

static DWORD svalue(char k, const char *name)
{
    WORD i, n;
    if (k == 'c' && name[0] == 'c' && name[1] == '.')
        return lastconv;
    if (*name != k)
        return num(name);
    n = (WORD)num(name + 1);
    for (i = 0; i < nsym; i++)
        if (sym[i].k == k && sym[i].n == n)
            return sym[i].v;
    return 0;
}

/* ---- DDEML instances and string handles ---- */

#define NINST 4
static DWORD ids[NINST];
static int ci;                          /* current slot */
#define ID ids[ci]

#define NHSZ 128
static struct { HSZ h; DWORD id; char s[64]; } hsz[NHSZ];
static WORD nhsz;

static void err(void)
{
    UINT e = DdeGetLastError(ID);
    if (e)
        P("%c   err=%s", letter, (LPSTR)errname(e));
}

static char *hszname(HSZ h)
{
    static char b[4][96];
    static int r;
    char *o = b[r++ & 3];
    WORD i;
    if (!h) {
        lstrcpy(o, "0");
        return o;
    }
    for (i = 0; i < nhsz; i++) {
        /* the atom may have gone and its number come back for another
           string: only an entry whose string the atom still has */
        char a[64];
        if (hsz[i].h == h && !HIWORD(h) && GlobalGetAtomName(LOWORD(h), a, sizeof a) &&
            !lstrcmpi(a, hsz[i].s))
            break;
    }
    if (i < nhsz)
        wsprintf(o, "\"%s\"", (LPSTR)hsz[i].s);
    else {
        char a[64];
        if (!HIWORD(h) && GlobalGetAtomName(LOWORD(h), a, sizeof a))
            wsprintf(o, "a\"%s\"", (LPSTR)a);
        else if (HIWORD(h) && GlobalGetAtomName(LOWORD(h), a, sizeof a)) {
            /* an instance-specific name: its string as DdeQueryString
               gives it */
            char q[64];
            DWORD n = DdeQueryString(ID, h, q, sizeof q, 0);
            q[n < sizeof q ? (WORD)n : sizeof q - 1] = 0;
            wsprintf(o, "i\"%s\"/\"%s\"", (LPSTR)a, (LPSTR)q);
        } else if (rawvals)
            wsprintf(o, "h?%08lX", (DWORD)h);
        else
            lstrcpy(o, "h?");
    }
    if (rawvals)
        wsprintf(o + lstrlen(o), "[%08lX]", (DWORD)h);
    return o;
}

static HSZ gethsz(const char *s, BOOL quoted)
{
    WORD i;
    HSZ h;
    if (!quoted && eq(s, "0"))
        return 0;
    for (i = 0; i < nhsz; i++)
        if (hsz[i].id == ID && !lstrcmp(hsz[i].s, s))
            return hsz[i].h;
    h = DdeCreateStringHandle(ID, (LPSTR)s, 0);
    if (nhsz < NHSZ && h) {
        hsz[nhsz].h = h;
        hsz[nhsz].id = ID;
        lstrcpyn(hsz[nhsz].s, s, sizeof hsz[0].s);
        nhsz++;
    }
    P("%c   (DdeCreateStringHandle %q) -> %s", letter, (LPSTR)s, (LPSTR)hszname(h));
    err();
    return h;
}

static HSZ arghsz(char **s)
{
    BOOL q;
    char *w = tok(s, &q);
    return gethsz(w, q);
}

/* the contents of a data handle, as text */
static void dumpdata(const char *what, HDDEDATA d)
{
    char b[260];
    DWORD n, i, m;
    char out[300];
    WORD o = 0;
    if (!d || d == (HDDEDATA)1 || d == CBR_BLOCK)
        return;
    n = DdeGetData(d, NULL, 0, 0);
    m = DdeGetData(d, (LPBYTE)b, n < 256 ? n : 256, 0);
    for (i = 0; i < m && o < sizeof out - 8; i++) {
        BYTE c = (BYTE)b[(WORD)i];
        if (c >= 32 && c < 127 && c != '\\' && c != '"')
            out[o++] = c;
        else {
            wsprintf(out + o, "\\x%02X", c);
            o += 4;
        }
    }
    out[o] = 0;
    P("%c   %s %s size=%lu \"%s\"", letter, (LPSTR)what, (LPSTR)sname('d', (DWORD)d), n, (LPSTR)out);
}

/* ---- callback answers ---- */

static char resp[16][96];
static char cbdo[16][160];

static void respdefault(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        resp[i][0] = 0;
        cbdo[i][0] = 0;
    }
    lstrcpy(resp[XTYP_ADVSTART >> 4 & 15], "1");
    lstrcpy(resp[XTYP_CONNECT >> 4 & 15], "1");
    lstrcpy(resp[XTYP_EXECUTE >> 4 & 15], "ack");
    lstrcpy(resp[XTYP_POKE >> 4 & 15], "ack");
    lstrcpy(resp[XTYP_ADVDATA >> 4 & 15], "ack");
    lstrcpy(resp[XTYP_REQUEST >> 4 & 15], "item");
    lstrcpy(resp[XTYP_ADVREQ >> 4 & 15], "item");
    lstrcpy(resp[XTYP_WILDCONNECT >> 4 & 15], "0");
}

static int xtindex(const char *s)
{
    int i;
    for (i = 0; i < 16; i++)
        if (eq(s, xtname(i << 4)))
            return i;
    return -1;
}

static void step(char *l);
static int depth;

static HDDEDATA answer(UINT type, UINT fmt, HSZ h2)
{
    int t = (type >> 4) & 15;
    char *r = resp[t];
    if (!*r)
        return 0;
    if (eq(r, "1"))
        return (HDDEDATA)1;
    if (eq(r, "0"))
        return 0;
    if (eq(r, "ack"))
        return (HDDEDATA)DDE_FACK;
    if (eq(r, "busy"))
        return (HDDEDATA)DDE_FBUSY;
    if (eq(r, "nack"))
        return (HDDEDATA)DDE_FNOTPROCESSED;
    if (eq(r, "block"))
        return CBR_BLOCK;
    if (prefix(r, "hex:"))
        return (HDDEDATA)hexnum(r + 4);
    if (prefix(r, "data:"))
        return (HDDEDATA)svalue('d', r + 5);
    if (eq(r, "item") || prefix(r, "text:")) {
        char b[128];
        HDDEDATA d;
        if (prefix(r, "text:"))
            lstrcpy(b, r + 5);
        else {
            char it[64];
            DWORD n = DdeQueryString(ID, h2, it, sizeof it, 0);
            it[n < sizeof it ? (WORD)n : sizeof it - 1] = 0;
            wsprintf(b, "%c-data-%s", letter, (LPSTR)it);
        }
        d = DdeCreateDataHandle(ID, b, lstrlen(b) + 1, 0, h2, fmt, 0);
        P("%c   (DdeCreateDataHandle %q) -> %s", letter, (LPSTR)b, (LPSTR)snew('d', (DWORD)d));
        return d;
    }
    if (prefix(r, "pairs:")) {
        HSZPAIR pr[9];
        int n = 0;
        char *s = r + 6, *e, sv[64];
        while (*s && n < 8) {
            for (e = s; *e && *e != '/'; e++)
                ;
            lstrcpyn(sv, s, (int)(e - s) + 1);
            pr[n].hszSvc = gethsz(sv, TRUE);
            s = *e ? e + 1 : e;
            for (e = s; *e && *e != ','; e++)
                ;
            lstrcpyn(sv, s, (int)(e - s) + 1);
            pr[n].hszTopic = gethsz(sv, TRUE);
            n++;
            s = *e ? e + 1 : e;
        }
        pr[n].hszSvc = pr[n].hszTopic = 0;
        {
            HDDEDATA d = DdeCreateDataHandle(ID, (LPVOID)pr, (n + 1) * sizeof(HSZPAIR), 0, 0, fmt, 0);
            P("%c   (DdeCreateDataHandle pairs %d) -> %s", letter, n, (LPSTR)snew('d', (DWORD)d));
            return d;
        }
    }
    return 0;
}

static void monitor(HDDEDATA hd, DWORD ev);

/* transaction identifiers: the counter in the high word is shown, the
   low word (an address inside the library) is not */
static char *xidname(DWORD id)
{
    static char b[2][24];
    static int r;
    char *o = b[r++ & 1];
    wsprintf(o, rawvals ? "x%04X[%08lX]" : "x%04X", HIWORD(id), id);
    return o;
}

static DWORD xids[64];
static WORD nxid;

static void addxid(DWORD id)
{
    if (nxid < 64)
        xids[nxid++] = id;
}

/* "xN": the last identifier seen with counter N; else a number */
static DWORD xidvalue(const char *s)
{
    WORD i, n;
    if (*s != 'x')
        return num(s);
    n = (WORD)hexnum(s + 1);
    for (i = nxid; i-- > 0;)
        if (HIWORD(xids[i]) == n)
            return xids[i];
    return 0;
}

static char *d2name(UINT type, DWORD d2)
{
    static char b[24];
    if (type == XTYP_ADVSTOP)
        return hszname((HSZ)d2);
    wsprintf(b, "%08lX", d2);
    return b;
}

static char *d1name(UINT type, DWORD d1)
{
    static char b[24];
    if (type == XTYP_XACT_COMPLETE)
        return xidname(d1);
    if ((type == XTYP_CONNECT || type == XTYP_WILDCONNECT) && d1)
        return "cc";
    wsprintf(b, "%08lX", d1);
    return b;
}

static HDDEDATA cbcommon(int slot, UINT type, UINT fmt, HCONV conv, HSZ h1, HSZ h2, HDDEDATA d,
                         DWORD d1, DWORD d2)
{
    HDDEDATA r;
    int t = (type >> 4) & 15, saved = ci;
    char who[4];
    ci = slot;
    who[0] = letter;
    who[1] = slot ? '.' : 0;
    who[2] = (char)('0' + slot);
    who[3] = 0;
    depth++;
    if (type == XTYP_MONITOR) {
        monitor(d, d2);
        depth--;
        ci = saved;
        return 0;
    }
    if (type == XTYP_XACT_COMPLETE)
        addxid(d1);
    P("%s cb %s(%x) fmt=%s conv=%s hsz1=%s hsz2=%s data=%s d1=%s d2=%s", (LPSTR)who,
      (LPSTR)xtname(type), type, (LPSTR)fmtname(fmt), (LPSTR)sname('c', (DWORD)conv),
      (LPSTR)hszname(h1), (LPSTR)hszname(h2),
      (LPSTR)(d == (HDDEDATA)1 ? "TRUE" : snew('d', (DWORD)d)),
      (LPSTR)d1name(type, d1), (LPSTR)d2name(type, d2));
    if ((type == XTYP_CONNECT || type == XTYP_WILDCONNECT) && d1) {
        CONVCONTEXT FAR *cc = (CONVCONTEXT FAR *)d1;
        P("%c   cc cb=%u flags=%u country=%u cp=%d lang=%lx sec=%lx", letter, cc->cb, cc->wFlags,
          cc->wCountryID, cc->iCodePage, cc->dwLangID, cc->dwSecurity);
    }
    if (type == XTYP_XACT_COMPLETE || type == XTYP_EXECUTE || type == XTYP_POKE ||
        type == XTYP_ADVDATA)
        dumpdata("in", d);
    if (cbdo[t][0]) {
        char l[160];
        if (conv)
            lastconv = (DWORD)conv;
        lstrcpy(l, cbdo[t]);
        cbdo[t][0] = 0;
        step(l);
    }
    r = answer(type, fmt, h2);
    if (type & XCLASS_DATA)
        P("%s   ret %s", (LPSTR)who, (LPSTR)sname('d', (DWORD)r));
    else if ((type & XCLASS_MASK) != XCLASS_NOTIFICATION || r)
        P("%s   ret=%lx", (LPSTR)who, (DWORD)r);
    depth--;
    ci = saved;
    return r;
}

HDDEDATA CALLBACK __export DdeCb0(UINT t, UINT f, HCONV c, HSZ h1, HSZ h2, HDDEDATA d, DWORD d1, DWORD d2)
{
    return cbcommon(0, t, f, c, h1, h2, d, d1, d2);
}
HDDEDATA CALLBACK __export DdeCb1(UINT t, UINT f, HCONV c, HSZ h1, HSZ h2, HDDEDATA d, DWORD d1, DWORD d2)
{
    return cbcommon(1, t, f, c, h1, h2, d, d1, d2);
}
HDDEDATA CALLBACK __export DdeCb2(UINT t, UINT f, HCONV c, HSZ h1, HSZ h2, HDDEDATA d, DWORD d1, DWORD d2)
{
    return cbcommon(2, t, f, c, h1, h2, d, d1, d2);
}
HDDEDATA CALLBACK __export DdeCb3(UINT t, UINT f, HCONV c, HSZ h1, HSZ h2, HDDEDATA d, DWORD d1, DWORD d2)
{
    return cbcommon(3, t, f, c, h1, h2, d, d1, d2);
}

/* ---- monitor ---- */

static void monitor(HDDEDATA hd, DWORD ev)
{
    DWORD cb;
    BYTE FAR *p;
    if (rawvals) {
        /* what the handle is: the event block's first bytes through
           DdeAccessData and through each word as a global handle */
        BYTE FAR *q;
        char b[80];
        int i;
        P("%c mon raw hData=%lx ev=%lx", letter, (DWORD)hd, ev);
        q = (BYTE FAR *)GlobalLock((HGLOBAL)HIWORD(hd));
        if (q) {
            b[0] = 0;
            for (i = 0; i < 24; i++)
                wsprintf(b + lstrlen(b), "%02X", q[i]);
            P("%c   lock(hi) size=%lu %s", letter, GlobalSize((HGLOBAL)HIWORD(hd)), (LPSTR)b);
            GlobalUnlock((HGLOBAL)HIWORD(hd));
        }
        q = (BYTE FAR *)GlobalLock((HGLOBAL)LOWORD(hd));
        if (q) {
            b[0] = 0;
            for (i = 0; i < 24; i++)
                wsprintf(b + lstrlen(b), "%02X", q[i]);
            P("%c   lock(lo) size=%lu %s", letter, GlobalSize((HGLOBAL)LOWORD(hd)), (LPSTR)b);
            GlobalUnlock((HGLOBAL)LOWORD(hd));
        }
    }
    p = DdeAccessDataF(hd, &cb);
    if (!p) {
        P("%c mon ev=%lx no data", letter, ev);
        return;
    }
    switch (ev) {
    case MF_HSZ_INFO: {
        MONHSZSTRUCT FAR *m = (MONHSZSTRUCT FAR *)p;
        P("%c mon HSZ cb=%u size=%lu action=%d hsz=%s task=%s str=%q", letter, m->cb, cb,
          m->fsAction, (LPSTR)hszname(m->hsz), (LPSTR)(m->hTask == GetCurrentTask() ? "self" : "other"),
          (LPSTR)m->str);
        break;
    }
    case MF_SENDMSGS:
    case MF_POSTMSGS: {
        MONMSGSTRUCT FAR *m = (MONMSGSTRUCT FAR *)p;
        P("%c mon %s cb=%u size=%lu to=%w msg=%x wp=%w lp=%lx", letter,
          (LPSTR)(ev == MF_SENDMSGS ? "SEND" : "POST"), m->cb, cb, m->hwndTo, m->wMsg,
          (WORD)m->wParam, (DWORD)m->lParam);
        break;
    }
    case MF_CALLBACKS: {
        MONCBSTRUCT FAR *m = (MONCBSTRUCT FAR *)p;
        P("%c mon CB cb=%u size=%lu type=%s fmt=%s conv=%s hsz1=%s hsz2=%s data=%s d1=%lx d2=%lx ret=%lx",
          letter, m->cb, cb, (LPSTR)xtname(m->wType), (LPSTR)fmtname(m->wFmt),
          (LPSTR)sname('c', (DWORD)m->hConv), (LPSTR)hszname(m->hsz1), (LPSTR)hszname(m->hsz2),
          (LPSTR)sname('d', (DWORD)m->hData), m->dwData1, m->dwData2, m->dwRet);
        break;
    }
    case MF_CONV: {
        MONCONVSTRUCT FAR *m = (MONCONVSTRUCT FAR *)p;
        P("%c mon CONV cb=%u size=%lu connect=%d svc=%s topic=%s cli=%s srv=%s", letter, m->cb, cb,
          m->fConnect, (LPSTR)hszname(m->hszSvc), (LPSTR)hszname(m->hszTopic),
          (LPSTR)sname('c', (DWORD)m->hConvClient), (LPSTR)sname('c', (DWORD)m->hConvServer));
        break;
    }
    case MF_LINKS: {
        MONLINKSTRUCT FAR *m = (MONLINKSTRUCT FAR *)p;
        P("%c mon LINK cb=%u size=%lu est=%d nodata=%d svc=%s topic=%s item=%s fmt=%s server=%d srv=%s cli=%s",
          letter, m->cb, cb, m->fEstablished, m->fNoData, (LPSTR)hszname(m->hszSvc),
          (LPSTR)hszname(m->hszTopic), (LPSTR)hszname(m->hszItem), (LPSTR)fmtname(m->wFmt),
          m->fServer, (LPSTR)sname('c', (DWORD)m->hConvServer), (LPSTR)sname('c', (DWORD)m->hConvClient));
        break;
    }
    case MF_ERRORS: {
        MONERRSTRUCT FAR *m = (MONERRSTRUCT FAR *)p;
        P("%c mon ERR cb=%u size=%lu err=%s", letter, m->cb, cb, (LPSTR)errname(m->wLastError));
        break;
    }
    default:
        P("%c mon ev=%lx cb=%u size=%lu", letter, ev, *(WORD FAR *)p, cb);
    }
    DdeUnaccessData(hd);
}

/* ---- the raw DDE window ---- */

#define NPART 16
static HWND part[NPART];                /* raw partners */
static WORD npart;
static int rack;                        /* 0 positive, 1 negative, 2 busy, 3 none */
static char rapp[64], rtopic[64];
static BOOL serving;
static struct { ATOM item; WORD cf; HWND to; WORD flags; } radvs[32];
static WORD nradv;

static void addpart(HWND h)
{
    WORD i;
    for (i = 0; i < npart; i++)
        if (part[i] == h)
            return;
    if (npart < NPART)
        part[npart++] = h;
}

static void delpart(HWND h)
{
    WORD i;
    for (i = 0; i < npart; i++)
        if (part[i] == h) {
            part[i] = part[--npart];
            return;
        }
}

static HGLOBAL ddeblock(WORD flags, WORD cf, const char *text)
{
    WORD n = lstrlen(text) + 1;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, n + 4);
    WORD FAR *p = h ? (WORD FAR *)GlobalLock(h) : NULL;
    if (!p)
        return NULL;
    p[0] = flags;
    p[1] = cf;
    lstrcpy((LPSTR)(p + 2), text);
    GlobalUnlock(h);
    return h;
}

static WORD ackstatus(void)
{
    return rack == 0 ? 0x8000 : rack == 2 ? 0x4000 : 0;
}

static BOOL atommatch(ATOM a, const char *want)
{
    char b[64];
    if (!a || eq(want, "*"))
        return TRUE;
    if (!GlobalGetAtomName(a, b, sizeof b))
        return FALSE;
    return eq(b, want);
}

LRESULT CALLBACK __export RawWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND from = (HWND)wp;
    switch (msg) {
    case WM_DDE_INITIATE:
        if (serving && from != w && atommatch(LOWORD(lp), rapp) && atommatch(HIWORD(lp), rtopic)) {
            addpart(from);
            SendMessage(from, WM_DDE_ACK, (WPARAM)w,
                        MAKELPARAM(GlobalAddAtom(LOWORD(lp) ? (LPSTR)rapp : (LPSTR)rapp),
                                   GlobalAddAtom(rtopic)));
        }
        return 0;
    case WM_DDE_ACK:
        if (InSendMessage()) {          /* answer to our INITIATE: keep all */
            addpart(from);
            return 0;
        }
        return 0;                       /* atoms, blocks: leaked (probe) */
    case WM_DDE_DATA: {
        HGLOBAL h = (HGLOBAL)LOWORD(lp);
        WORD FAR *p = h ? (WORD FAR *)GlobalLock(h) : NULL;
        WORD flags = p ? p[0] : 0;
        if (p)
            GlobalUnlock(h);
        if ((flags & 0x8000) && rack != 3)
            PostMessage(from, WM_DDE_ACK, (WPARAM)w, MAKELPARAM(ackstatus(), HIWORD(lp)));
        if (h && (flags & 0x2000) && rack == 0)
            GlobalFree(h);
        return 0;
    }
    case WM_DDE_REQUEST: {
        char t[96], it[64];
        it[0] = 0;
        GlobalGetAtomName(HIWORD(lp), it, sizeof it);
        if (rack != 0) {
            if (rack != 3)
                PostMessage(from, WM_DDE_ACK, (WPARAM)w, MAKELPARAM(ackstatus(), HIWORD(lp)));
            return 0;
        }
        wsprintf(t, "raw-%c-%s", letter, (LPSTR)it);
        PostMessage(from, WM_DDE_DATA, (WPARAM)w,
                    MAKELPARAM(ddeblock(0x3000, LOWORD(lp), t), HIWORD(lp)));
        return 0;
    }
    case WM_DDE_POKE:
    case WM_DDE_ADVISE:
    case WM_DDE_UNADVISE:
        if (msg == WM_DDE_ADVISE && rack == 0 && nradv < 32) {
            WORD FAR *o = (WORD FAR *)GlobalLock((HGLOBAL)LOWORD(lp));
            radvs[nradv].item = HIWORD(lp);
            radvs[nradv].cf = o ? o[1] : 0;
            radvs[nradv].flags = o ? o[0] : 0;
            radvs[nradv].to = from;
            nradv++;
            if (o)
                GlobalUnlock((HGLOBAL)LOWORD(lp));
        }
        if (rack != 3)
            PostMessage(from, WM_DDE_ACK, (WPARAM)w, MAKELPARAM(ackstatus(), HIWORD(lp)));
        return 0;
    case WM_DDE_EXECUTE:
        if (rack != 3)
            PostMessage(from, WM_DDE_ACK, (WPARAM)w, MAKELPARAM(ackstatus(), HIWORD(lp)));
        return 0;
    case WM_DDE_TERMINATE:
        if (IsWindow(from))
            PostMessage(from, WM_DDE_TERMINATE, (WPARAM)w, 0);
        delpart(from);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

static void rawpost(UINT msg, LPARAM lp)
{
    WORD i;
    if (!npart) {
        P("%c   no raw partner", letter);
        return;
    }
    for (i = 0; i < npart; i++)
        if (!PostMessage(part[i], msg, (WPARAM)rawwnd, lp))
            P("%c   post failed", letter);
}

/* ---- windows of DDEML ---- */

static BOOL isddeml(HWND h)
{
    char mod[80];
    HINSTANCE hi = (HINSTANCE)GetWindowWord(h, GWW_HINSTANCE);
    int i, b = 0;
    mod[0] = 0;
    if (hi)
        GetModuleFileName(hi, mod, sizeof mod);
    for (i = 0; mod[i]; i++)
        if (mod[i] == '\\')
            b = i + 1;
    return prefix(mod + b, "DDEM");
}

static void winfo(HWND h, int depth)
{
    char t[64], c[32], b[200];
    HWND k;
    int i, n;
    t[0] = c[0] = 0;
    GetWindowText(h, t, sizeof t);
    GetClassName(h, c, sizeof c);
    n = GetClassWord(h, GCW_CBWNDEXTRA);
    b[0] = 0;
    if (rawvals)
        for (i = 0; i + 1 < n && i < 40; i += 2)
            wsprintf(b + lstrlen(b), " %04X", GetWindowWord(h, i));
    if (rawvals)
        P("%c   hwnd=%x", letter, (WORD)h);
    P("%c WIN %d %w class=%q title=%q style=%lx cls.style=%x cbwnd=%d cbcls=%d id=%x parent=%w "
      "owner=%w task=%s words%s", letter, depth, h, (LPSTR)c, (LPSTR)t, GetWindowLong(h, GWL_STYLE),
      GetClassWord(h, GCW_STYLE), n, GetClassWord(h, GCW_CBCLSEXTRA), GetWindowWord(h, GWW_ID),
      GetParent(h), GetWindow(h, GW_OWNER),
      (LPSTR)(GetWindowTask(h) == GetCurrentTask() ? "self" : "other"), (LPSTR)b);
    for (k = GetWindow(h, GW_CHILD); k; k = GetWindow(k, GW_HWNDNEXT))
        winfo(k, depth + 1);
}

/* ---- commands ---- */

static HWND peer(char x)
{
    char t[8];
    wsprintf(t, "DM %c", x);
    return FindWindow("DMProbe", t);
}

static void sendto(char x, const char *cmd)
{
    HWND h = peer(x);
    ATOM a;
    if (!h) {
        P("%c no copy %c", letter, x);
        return;
    }
    a = GlobalAddAtom(cmd);
    done = FALSE;
    PostMessage(h, WM_DMCMD, (WPARAM)me, MAKELPARAM(a, 0));
    pump(60000, &done);
    if (!done)
        P("%c copy %c did not answer", letter, x);
}

static UINT xtype(char *s)
{
    UINT t = 0;
    char *p;
    for (p = s; *p && *p != '+'; p++)
        ;
    if (*p)
        *p++ = 0;
    if (eq(s, "request"))
        t = XTYP_REQUEST;
    else if (eq(s, "poke"))
        t = XTYP_POKE;
    else if (eq(s, "exec"))
        t = XTYP_EXECUTE;
    else if (eq(s, "advstart"))
        t = XTYP_ADVSTART;
    else if (eq(s, "advstop"))
        t = XTYP_ADVSTOP;
    else if (isdig(*s))
        t = (UINT)num(s);
    while (*p) {
        char *f = p;
        for (; *p && *p != '+'; p++)
            ;
        if (*p)
            *p++ = 0;
        if (eq(f, "nodata"))
            t |= XTYPF_NODATA;
        else if (eq(f, "ackreq"))
            t |= XTYPF_ACKREQ;
    }
    return t;
}

static void convinfo(HCONV c, DWORD id)
{
    CONVINFO i;
    UINT r;
    i.cb = sizeof i;
    r = DdeQueryConvInfo(c, id, &i);
    P("%c DdeQueryConvInfo(%s, %s) -> %u", letter, (LPSTR)sname('c', (DWORD)c),
      (LPSTR)(id == QID_SYNC ? "sync" : xidname(id)), r);
    if (r)
        P("%c   cb=%lu user=%lx partner=%s svcpartner=%s svcreq=%s topic=%s item=%s fmt=%s type=%x "
          "status=%x convst=%u lasterr=%s list=%s cc=%u/%u/%u/%d/%lx/%lx",
          letter, i.cb, i.hUser, (LPSTR)sname('c', (DWORD)i.hConvPartner),
          (LPSTR)hszname(i.hszSvcPartner), (LPSTR)hszname(i.hszServiceReq),
          (LPSTR)hszname(i.hszTopic), (LPSTR)hszname(i.hszItem), (LPSTR)fmtname(i.wFmt), i.wType,
          i.wStatus, i.wConvst, (LPSTR)errname(i.wLastError), (LPSTR)sname('l', (DWORD)i.hConvList),
          i.ConvCtxt.cb, i.ConvCtxt.wFlags, i.ConvCtxt.wCountryID, i.ConvCtxt.iCodePage,
          i.ConvCtxt.dwLangID, i.ConvCtxt.dwSecurity);
    err();
}

static CONVCONTEXT ccx = { sizeof(CONVCONTEXT), 7, 49, 850, 0x0407, 0x12345678L };

static void step(char *l)
{
    char *a = l, *w;
    BOOL q;
    if (*l == ';' || !*l)
        return;
    if (l[0] && l[1] == ':' && l[0] != letter) {
        char *c = l + 2;
        while (*c == ' ')
            c++;
        sendto(l[0], c);
        return;
    }
    if (l[0] && l[1] == ':') {
        l += 2;
        while (*l == ' ')
            l++;
        a = l;
    }
    P("%c> %s", letter, (LPSTR)l);
    w = tok(&a, NULL);
    if (eq(w, "start") || eq(w, "startx")) {
        char cmd[32], *x = tok(&a, NULL);
        wsprintf(cmd, "C:\\%s.EXE %s", (LPSTR)(eq(w, "start") ? "DM" : "DMX"), (LPSTR)x);
        if (WinExec(cmd, SW_SHOWMINNOACTIVE) < 32)
            P("%c   WinExec failed", letter);
        else {
            DWORD end = GetTickCount() + 30000;
            while (!peer(x[0]) && (long)(GetTickCount() - end) < 0)
                pump(50, NULL);
            if (!peer(x[0]))
                P("%c   %c did not start", letter, x[0]);
        }
    } else if (eq(w, "quit")) {
        char *x = tok(&a, NULL);
        HWND h = peer(x[0]);
        DWORD end = GetTickCount() + 30000;
        if (h)
            PostMessage(h, WM_CLOSE, 0, 0);
        while (peer(x[0]) && (long)(GetTickCount() - end) < 0)
            pump(50, NULL);
    } else if (eq(w, "wait")) {
        pump(num(tok(&a, NULL)), NULL);
    } else if (eq(w, "settle")) {
        settle = num(tok(&a, NULL));
    } else if (eq(w, "raw")) {
        rawvals = eq(tok(&a, NULL), "on");
    } else if (eq(w, "run")) {
        if (WinExec(a, SW_SHOWNORMAL) < 32)
            P("%c   WinExec failed", letter);
    } else if (eq(w, "inst")) {
        ci = (int)num(tok(&a, NULL)) % NINST;
    } else if (eq(w, "wins")) {
        HWND h;
        for (h = GetWindow(GetDesktopWindow(), GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
            if (isddeml(h))
                winfo(h, 0);
    } else if (eq(w, "cls")) {
        /* DDEML's window classes: GetClassInfo by name, with the
           module's instance */
        HINSTANCE hi = GetModuleHandle("DDEML");
        char *n;
        WNDCLASS wc;
        while (*(n = tok(&a, &q)) != 0) {
            if (GetClassInfo(hi, n, &wc))
                P("%c   class %q style=%x cbcls=%d cbwnd=%d", letter, (LPSTR)n, wc.style,
                  wc.cbClsExtra, wc.cbWndExtra);
            else
                P("%c   class %q none", letter, (LPSTR)n);
        }
    } else if (eq(w, "init") || eq(w, "reinit")) {
        char *f = tok(&a, NULL);
        DWORD af = *f ? hexnum(f) : 0;
        UINT r;
        static FARPROC cb[NINST];
        if (!cb[ci])
            cb[ci] = MakeProcInstance(ci == 0 ? (FARPROC)DdeCb0 : ci == 1 ? (FARPROC)DdeCb1
                                      : ci == 2 ? (FARPROC)DdeCb2 : (FARPROC)DdeCb3, inst);
        if (eq(w, "init"))
            ID = 0;
        r = DdeInitialize(&ID, (PFNCALLBACK)cb[ci], af, 0);
        P("%c DdeInitialize(%lx) -> %s id=%s", letter, af, (LPSTR)errname(r), (LPSTR)sname('i', ID));
    } else if (eq(w, "uninit")) {
        BOOL r = DdeUninitialize(ID);
        P("%c DdeUninitialize(%s) -> %d", letter, (LPSTR)sname('i', ID), r);
    } else if (eq(w, "lasterr")) {
        char *x = tok(&a, NULL);
        DWORD id = *x ? svalue('i', x) : ID;
        P("%c DdeGetLastError -> %s", letter, (LPSTR)errname(DdeGetLastError(id)));
    } else if (eq(w, "hsz")) {
        char *s = tok(&a, &q), *cp = tok(&a, NULL);
        HSZ h = DdeCreateStringHandle(ID, s, (int)num(cp));
        P("%c DdeCreateStringHandle(%q, %d) -> %s", letter, (LPSTR)s, (int)num(cp), (LPSTR)hszname(h));
        if (h && nhsz < NHSZ) {
            WORD i;
            for (i = 0; i < nhsz; i++)
                if (hsz[i].h == h && hsz[i].id == ID)
                    break;
            if (i == nhsz) {
                hsz[nhsz].h = h;
                hsz[nhsz].id = ID;
                lstrcpyn(hsz[nhsz].s, s, sizeof hsz[0].s);
                nhsz++;
            }
        }
        err();
    } else if (eq(w, "hszval")) {
        char *s = tok(&a, &q);
        HSZ h = gethsz(s, q);
        P("%c   atom-match=%d", letter, GlobalFindAtom(s) == LOWORD(h) && !HIWORD(h));
    } else if (eq(w, "atomq")) {
        /* the global atom of a string: does it exist, and how many
           GlobalDeleteAtom calls remove it (they are put back after) */
        char *s = tok(&a, &q);
        ATOM at = GlobalFindAtom(s);
        int n = 0, k;
        while (at && GlobalFindAtom(s) == at && n < 50) {
            GlobalDeleteAtom(at);
            n++;
        }
        for (k = 0; k < n; k++)
            GlobalAddAtom(s);
        P("%c   atom %q refs=%d%s", letter, (LPSTR)s, n,
          (LPSTR)(n && GlobalFindAtom(s) != at ? " (moved)" : ""));
    } else if (eq(w, "freehsz") || eq(w, "keephsz")) {
        HSZ h = arghsz(&a);
        BOOL r = eq(w, "freehsz") ? DdeFreeStringHandle(ID, h) : DdeKeepStringHandle(ID, h);
        /* (Keep returns the atom: shown as such, its value is USER's) */
        P("%c %s(%s) -> %s", letter, (LPSTR)(eq(w, "freehsz") ? "DdeFreeStringHandle" : "DdeKeepStringHandle"),
          (LPSTR)hszname(h), (LPSTR)(h && (WORD)r == LOWORD(h) && r != 1 ? "the atom"
                                     : r ? "1" : "0"));
        err();
    } else if (eq(w, "qstr")) {
        HSZ h = arghsz(&a);
        char *m = tok(&a, NULL), b[80];
        DWORD max = *m ? num(m) : sizeof b, r, r0;
        lstrcpy(b, "########");
        r0 = DdeQueryString(ID, h, NULL, 0, 0);
        r = DdeQueryString(ID, h, b, max, 0);
        P("%c DdeQueryString(%s, %lu) -> %lu %q (NULL buffer: %lu)", letter, (LPSTR)hszname(h), max,
          r, (LPSTR)b, r0);
        err();
    } else if (eq(w, "cmp")) {
        HSZ h1 = arghsz(&a), h2 = arghsz(&a);
        P("%c DdeCmpStringHandles(%s, %s) -> %d", letter, (LPSTR)hszname(h1), (LPSTR)hszname(h2),
          DdeCmpStringHandles(h1, h2));
    } else if (eq(w, "ns")) {
        HSZ h = arghsz(&a);
        char *c = tok(&a, NULL);
        UINT f = eq(c, "reg") ? DNS_REGISTER : eq(c, "unreg") ? DNS_UNREGISTER
               : eq(c, "on") ? DNS_FILTERON : eq(c, "off") ? DNS_FILTEROFF : (UINT)num(c);
        HDDEDATA r = DdeNameService(ID, h, 0, f);
        P("%c DdeNameService(%s, %x) -> %lx", letter, (LPSTR)hszname(h), f, (DWORD)r);
        err();
    } else if (eq(w, "conn") || eq(w, "connlist")) {
        HSZ s = arghsz(&a), t = arghsz(&a);
        char *x = tok(&a, NULL);
        if (eq(w, "conn")) {
            HCONV c = DdeConnect(ID, s, t, eq(x, "cc") ? &ccx : NULL);
            if (c)
                lastconv = (DWORD)c;
            P("%c DdeConnect(%s, %s) -> %s", letter, (LPSTR)hszname(s), (LPSTR)hszname(t),
              (LPSTR)sname('c', (DWORD)c));
        } else {
            HCONVLIST old = 0, r;
            if (*x == 'l') {
                old = (HCONVLIST)svalue('l', x);
                x = tok(&a, NULL);
            }
            r = DdeConnectList(ID, s, t, old, eq(x, "cc") ? &ccx : NULL);
            P("%c DdeConnectList(%s, %s, %s) -> %s", letter, (LPSTR)hszname(s), (LPSTR)hszname(t),
              (LPSTR)sname('l', (DWORD)old), (LPSTR)sname('l', (DWORD)r));
        }
        err();
    } else if (eq(w, "next")) {
        HCONVLIST cl = (HCONVLIST)svalue('l', tok(&a, NULL));
        char *x = tok(&a, NULL);
        HCONV prev = *x ? (HCONV)svalue('c', x) : 0;
        HCONV r = DdeQueryNextServer(cl, prev);
        if (r)
            lastconv = (DWORD)r;
        P("%c DdeQueryNextServer(%s, %s) -> %s", letter, (LPSTR)sname('l', (DWORD)cl),
          (LPSTR)sname('c', (DWORD)prev), (LPSTR)sname('c', (DWORD)r));
        err();
    } else if (eq(w, "disc") || eq(w, "reconn")) {
        HCONV c = (HCONV)svalue('c', tok(&a, NULL));
        if (eq(w, "disc"))
            P("%c DdeDisconnect(%s) -> %d", letter, (LPSTR)sname('c', (DWORD)c), DdeDisconnect(c));
        else {
            HCONV r = DdeReconnect(c);
            if (r)
                lastconv = (DWORD)r;
            P("%c DdeReconnect(%s) -> %s", letter, (LPSTR)sname('c', (DWORD)c), (LPSTR)sname('c', (DWORD)r));
        }
        err();
    } else if (eq(w, "disclist")) {
        HCONVLIST cl = (HCONVLIST)svalue('l', tok(&a, NULL));
        P("%c DdeDisconnectList(%s) -> %d", letter, (LPSTR)sname('l', (DWORD)cl), DdeDisconnectList(cl));
        err();
    } else if (eq(w, "cwin")) {
        DWORD c = svalue('c', tok(&a, NULL));
        P("%c   conv %s low=%w high=%x", letter, (LPSTR)sname('c', c), LOWORD(c), HIWORD(c));
    } else if (eq(w, "status")) {
        HCONV c = (HCONV)svalue('c', tok(&a, NULL));
        CONVINFO i;
        UINT r;
        i.cb = sizeof i;
        r = DdeQueryConvInfo(c, QID_SYNC, &i);
        P("%c DdeQueryConvInfo(%s) -> %u status=%x convst=%u user=%lx", letter,
          (LPSTR)sname('c', (DWORD)c), r, r ? i.wStatus : 0, r ? i.wConvst : 0, r ? i.hUser : 0L);
        err();
    } else if (eq(w, "info")) {
        HCONV c = (HCONV)svalue('c', tok(&a, NULL));
        char *x = tok(&a, NULL);
        convinfo(c, *x ? xidvalue(x) : QID_SYNC);
    } else if (eq(w, "setuser")) {
        HCONV c = (HCONV)svalue('c', tok(&a, NULL));
        char *x = tok(&a, NULL);
        DWORD id = eq(x, "sync") ? QID_SYNC : xidvalue(x), v = hexnum(tok(&a, NULL));
        P("%c DdeSetUserHandle(%s, %s, %lx) -> %d", letter, (LPSTR)sname('c', (DWORD)c), (LPSTR)x, v,
          DdeSetUserHandle(c, id, v));
        err();
    } else if (eq(w, "xact")) {
        HCONV c = (HCONV)svalue('c', tok(&a, NULL));
        UINT t = xtype(tok(&a, NULL));
        HSZ it = arghsz(&a);
        UINT f = parsefmt(tok(&a, NULL));
        char *d = tok(&a, &q), *to = tok(&a, NULL);
        LPVOID p = NULL;
        DWORD cb = 0, tmo = eq(to, "async") ? TIMEOUT_ASYNC : num(to), res = 0xDEADBEEFL;
        HDDEDATA r;
        if (prefix(d, "text:")) {
            p = d + 5;
            cb = lstrlen(d + 5) + 1;
        } else if (*d == 'd') {
            p = (LPVOID)svalue('d', d);
            cb = (DWORD)-1;
        }
        r = DdeClientTransaction(p, cb, c, it, f, t, tmo, &res);
        if (tmo == TIMEOUT_ASYNC && r) {
            addxid(res);
            P("%c DdeClientTransaction(%s, %s(%x), %s, %s, %s, async) -> %s id=%s", letter,
              (LPSTR)sname('c', (DWORD)c), (LPSTR)xtname(t), t, (LPSTR)hszname(it),
              (LPSTR)fmtname(f), (LPSTR)d, (LPSTR)(r == (HDDEDATA)1 ? "TRUE" : snew('d', (DWORD)r)),
              (LPSTR)xidname(res));
        } else
            P("%c DdeClientTransaction(%s, %s(%x), %s, %s, %s, %lx) -> %s res=%lx", letter,
              (LPSTR)sname('c', (DWORD)c), (LPSTR)xtname(t), t, (LPSTR)hszname(it),
              (LPSTR)fmtname(f), (LPSTR)d, tmo,
              (LPSTR)(r == (HDDEDATA)1 ? "TRUE" : snew('d', (DWORD)r)), res);
        if (r && r != (HDDEDATA)1)
            dumpdata("out", r);
        err();
    } else if (eq(w, "abandon")) {
        HCONV c = (HCONV)svalue('c', tok(&a, NULL));
        char *x = tok(&a, NULL);
        DWORD id = xidvalue(x);
        P("%c DdeAbandonTransaction(%s, %s) -> %d", letter, (LPSTR)sname('c', (DWORD)c), (LPSTR)x,
          DdeAbandonTransaction(ID, c, id));
        err();
    } else if (eq(w, "postadv")) {
        HSZ t = arghsz(&a), i = arghsz(&a);
        P("%c DdePostAdvise(%s, %s) ...", letter, (LPSTR)hszname(t), (LPSTR)hszname(i));
        P("%c DdePostAdvise -> %d", letter, DdePostAdvise(ID, t, i));
        err();
    } else if (eq(w, "enable")) {
        HCONV c = (HCONV)svalue('c', tok(&a, NULL));
        char *x = tok(&a, NULL);
        UINT cmd = eq(x, "all") ? EC_ENABLEALL : eq(x, "one") ? EC_ENABLEONE
                 : eq(x, "off") ? EC_DISABLE : eq(x, "query") ? EC_QUERYWAITING : (UINT)num(x);
        P("%c DdeEnableCallback(%s, %x) ...", letter, (LPSTR)sname('c', (DWORD)c), cmd);
        P("%c DdeEnableCallback -> %d", letter, DdeEnableCallback(ID, c, cmd));
        err();
    } else if (eq(w, "mkdata")) {
        char *n = tok(&a, NULL), *t = tok(&a, &q);
        HSZ it = arghsz(&a);
        UINT f = parsefmt(tok(&a, NULL));
        char *o = tok(&a, NULL);
        HDDEDATA d = DdeCreateDataHandle(ID, eq(t, "null") ? NULL : t, lstrlen(t) + 1, 0, it, f,
                                         eq(o, "own") ? HDATA_APPOWNED : (UINT)num(o));
        if (d)
            setname('d', (DWORD)d, n);
        P("%c DdeCreateDataHandle(%q, %s, %s, %s) -> %s", letter, (LPSTR)t, (LPSTR)hszname(it),
          (LPSTR)fmtname(f), (LPSTR)o, (LPSTR)sname('d', (DWORD)d));
        err();
    } else if (eq(w, "hdata")) {
        char *n = tok(&a, NULL);
        setname('d', hexnum(tok(&a, NULL)), n);
    } else if (eq(w, "adddata")) {
        HDDEDATA d = (HDDEDATA)svalue('d', tok(&a, NULL)), r;
        char *t = tok(&a, &q);
        DWORD off = num(tok(&a, NULL));
        r = DdeAddData(d, t, lstrlen(t) + 1, off);
        P("%c DdeAddData(%s, %q, %lu) -> %s", letter, (LPSTR)sname('d', (DWORD)d), (LPSTR)t, off,
          (LPSTR)sname('d', (DWORD)r));
        err();
    } else if (eq(w, "getdata")) {
        HDDEDATA d = (HDDEDATA)svalue('d', tok(&a, NULL));
        char *m = tok(&a, NULL), *o = tok(&a, NULL), b[128];
        DWORD max = *m ? num(m) : sizeof b - 1, off = num(o), r;
        lstrcpy(b, "########");
        r = DdeGetData(d, (LPBYTE)b, max, off);
        b[sizeof b - 1] = 0;
        P("%c DdeGetData(%s, %lu, %lu) -> %lu %q", letter, (LPSTR)sname('d', (DWORD)d), max, off, r,
          (LPSTR)b);
        err();
        dumpdata("is", d);
    } else if (eq(w, "access")) {
        HDDEDATA d = (HDDEDATA)svalue('d', tok(&a, NULL));
        DWORD cb = 0xFFFFL;
        LPBYTE p = DdeAccessDataF(d, &cb);
        P("%c DdeAccessData(%s) -> %s size=%lu %q", letter, (LPSTR)sname('d', (DWORD)d),
          (LPSTR)(p ? "ptr" : "NULL"), cb, p ? (LPSTR)p : (LPSTR)"");
        if (p && rawvals)
            P("%c   ptr=%lx handle-of-sel=%x size=%lu", letter, (DWORD)p,
              (WORD)GlobalHandle(SELECTOROF(p)), GlobalSize((HGLOBAL)GlobalHandle(SELECTOROF(p))));
        err();
    } else if (eq(w, "unaccess")) {
        HDDEDATA d = (HDDEDATA)svalue('d', tok(&a, NULL));
        P("%c DdeUnaccessData(%s) -> %d", letter, (LPSTR)sname('d', (DWORD)d), DdeUnaccessData(d));
        err();
    } else if (eq(w, "freedata")) {
        HDDEDATA d = (HDDEDATA)svalue('d', tok(&a, NULL));
        P("%c DdeFreeDataHandle(%s) -> %d", letter, (LPSTR)sname('d', (DWORD)d), DdeFreeDataHandle(d));
        err();
    } else if (eq(w, "ret") || eq(w, "cbdo")) {
        char *t = tok(&a, NULL);
        int i = xtindex(t);
        while (*a == ' ')
            a++;
        if (i < 0)
            P("%c   bad type", letter);
        else
            lstrcpyn(eq(w, "ret") ? resp[i] : cbdo[i], a, eq(w, "ret") ? sizeof resp[0] : sizeof cbdo[0]);
    } else if (eq(w, "rinit")) {
        char *ap = tok(&a, &q), *tp;
        ATOM aa, at;
        BOOL q2;
        tp = tok(&a, &q2);
        aa = (eq(ap, "0") && !q) ? 0 : GlobalAddAtom(ap);
        at = (eq(tp, "0") && !q2) ? 0 : GlobalAddAtom(tp);
        npart = 0;
        SendMessage((HWND)-1, WM_DDE_INITIATE, (WPARAM)rawwnd, MAKELPARAM(aa, at));
        if (aa)
            GlobalDeleteAtom(aa);
        if (at)
            GlobalDeleteAtom(at);
        P("%c   raw partners %u", letter, npart);
    } else if (eq(w, "rserve")) {
        char *ap = tok(&a, NULL);
        lstrcpy(rapp, ap);
        lstrcpy(rtopic, tok(&a, NULL));
        serving = TRUE;
    } else if (eq(w, "rack")) {
        char *x = tok(&a, NULL);
        rack = eq(x, "pos") ? 0 : eq(x, "neg") ? 1 : eq(x, "busy") ? 2 : 3;
    } else if (eq(w, "rexec")) {
        while (*a == ' ')
            a++;
        {
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, lstrlen(a) + 1);
            LPSTR p = GlobalLock(h);
            lstrcpy(p, a);
            GlobalUnlock(h);
            rawpost(WM_DDE_EXECUTE, MAKELPARAM(0, h));
        }
    } else if (eq(w, "rreq") || eq(w, "runadv")) {
        char *it = tok(&a, NULL);
        UINT f = parsefmt(tok(&a, NULL));
        rawpost(eq(w, "rreq") ? WM_DDE_REQUEST : WM_DDE_UNADVISE, MAKELPARAM(f, GlobalAddAtom(it)));
    } else if (eq(w, "rpoke")) {
        char *it = tok(&a, NULL);
        UINT f = parsefmt(tok(&a, NULL));
        char *t = tok(&a, &q);
        rawpost(WM_DDE_POKE, MAKELPARAM(ddeblock(0x2000, f, t), GlobalAddAtom(it)));
    } else if (eq(w, "radv")) {
        char *it = tok(&a, NULL);
        UINT f = parsefmt(tok(&a, NULL));
        WORD fl = (WORD)hexnum(tok(&a, NULL));
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 4);
        WORD FAR *p = (WORD FAR *)GlobalLock(h);
        p[0] = fl;
        p[1] = f;
        GlobalUnlock(h);
        rawpost(WM_DDE_ADVISE, MAKELPARAM(h, GlobalAddAtom(it)));
    } else if (eq(w, "radvdata")) {
        char *it = tok(&a, NULL);
        UINT f = parsefmt(tok(&a, NULL));
        char *t = tok(&a, &q);
        WORD i;
        for (i = 0; i < nradv; i++) {
            char b[64];
            GlobalGetAtomName(radvs[i].item, b, sizeof b);
            if (eq(b, it) && radvs[i].cf == f && IsWindow(radvs[i].to))
                PostMessage(radvs[i].to, WM_DDE_DATA, (WPARAM)rawwnd,
                            MAKELPARAM((radvs[i].flags & 0x4000) ? 0
                                       : ddeblock((radvs[i].flags & 0x8000) | 0x2000, f, t),
                                       GlobalAddAtom(it)));
        }
    } else if (eq(w, "rterm")) {
        rawpost(WM_DDE_TERMINATE, 0);
        pump(300, NULL);
        npart = 0;
    } else
        P("%c   unknown command", letter);
    if (!depth)
        pump(settle, NULL);
}

LRESULT CALLBACK __export ProbeWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DMCMD: {
        char l[300];
        HWND from = (HWND)wp;
        l[0] = 0;
        GlobalGetAtomName(LOWORD(lp), l, sizeof l);
        GlobalDeleteAtom(LOWORD(lp));
        step(l);
        PostMessage(from, WM_DMDONE, 0, 0);
        return 0;
    }
    case WM_DMDONE:
        done = TRUE;
        return 0;
    case WM_CLOSE:
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

static char script[16384];

int FAR __cdecl tmain(void)
{
    WNDCLASS wc;
    char FAR *cl;
    char title[8];
    int i;
    inst = (HINSTANCE)init_regs[5];
    cl = (char FAR *)MAKELP(init_regs[6], init_regs[1]);
    WaitEvent(0);
    InitApp(inst);
    for (i = 0; cl[i] == ' '; i++)
        ;
    if (cl[i] >= 'A' && cl[i] <= 'Z')
        letter = cl[i];
    respdefault();
    if (letter == 'A') {
        wc.style = 0;
        wc.lpfnWndProc = ProbeWndProc;
        wc.cbClsExtra = wc.cbWndExtra = 0;
        wc.hInstance = inst;
        wc.hIcon = NULL;
        wc.hCursor = NULL;
        wc.hbrBackground = NULL;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "DMProbe";
        RegisterClass(&wc);
        wc.lpfnWndProc = RawWndProc;
        wc.lpszClassName = "DMRaw";
        RegisterClass(&wc);
    } else {
        /* the classes are the first copy's (a second instance of a
           module shares them); DMX.EXE is another module: its own */
        if (!GetClassInfo(inst, "DMProbe", &wc)) {
            wc.style = 0;
            wc.lpfnWndProc = ProbeWndProc;
            wc.cbClsExtra = wc.cbWndExtra = 0;
            wc.hInstance = inst;
            wc.hIcon = NULL;
            wc.hCursor = NULL;
            wc.hbrBackground = NULL;
            wc.lpszMenuName = NULL;
            wc.lpszClassName = "DMProbe";
            RegisterClass(&wc);
            wc.lpfnWndProc = RawWndProc;
            wc.lpszClassName = "DMRaw";
            RegisterClass(&wc);
        }
    }
    wsprintf(title, "DM %c", letter);
    me = CreateWindow("DMProbe", title, WS_OVERLAPPED, 0, 0, 10, 10, NULL, NULL, inst, NULL);
    wsprintf(title, "Raw %c", letter);
    rawwnd = CreateWindow("DMRaw", title, WS_OVERLAPPED, 0, 0, 10, 10, NULL, NULL, inst, NULL);
    if (letter != 'A') {
        MSG m;
        ctl = peer('A');
        while (GetMessage(&m, NULL, 0, 0)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        return 0;
    }
    {
        HFILE f;
        int n;
        char *l, *e;
        OsSpyStart();
        OsSpySent(TRUE);
        P("A start");
        f = _lopen("C:\\DM.TXT", OF_READ);
        if (f == HFILE_ERROR) {
            P("A no script");
            return 1;
        }
        n = _lread(f, script, sizeof script - 1);
        _lclose(f);
        script[n < 0 ? 0 : n] = 0;
        for (l = script; *l; l = e) {
            e = l;
            while (*e && *e != '\r' && *e != '\n')
                e++;
            while (*e == '\r' || *e == '\n')
                *e++ = 0;
            step(l);
        }
        P("A end");
        OsSpyStop();
    }
    return 0;
}
