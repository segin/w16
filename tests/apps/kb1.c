/*
 * kb1.c - KB1: every function KEYBOARD.DRV exports, called from a program
 * over all its inputs and logged (drivers/keyboard/README.md). Run as the
 * shell (tests/kbprobe.sh), once with Windows 3.1's KEYBOARD.DRV and once
 * with w16's; the two TK.LOGs must be identical (lines "OBS ~..." are
 * layout-dependent and left out of the comparison).
 *
 * Every call goes through md_call (md1s.asm), which pushes 16 words and
 * reports how many bytes the function took off: each entry's "popped" is
 * logged once (a wrong count corrupts every caller), and a wrong guess of
 * the argument list here cannot corrupt the probe's stack.
 *
 * Logged, in this order: the entry points; Inquire's KBINFO; GetKeyboardType
 * (0-3); GetKBCodePage; AnsiToOem and OemToAnsi of the string 1..255 and
 * AnsiToOemBuff/OemToAnsiBuff of 0..255, in place too, and across a 64K
 * boundary (a huge string); VkKeyScan and OemKeyScan of 0..255;
 * MapVirtualKey(0..255 and a few above, types 0-4); GetKeyNameText for
 * every scan code with and without the extended and "don't care" bits,
 * and with short buffers; ToAscii for every virtual key in 14 shift
 * states (key state arrays of the probe's own), key-up and extended
 * variants, KeyFlags, Alt+keypad sequences with real scan codes, VK 0;
 * SetSpeed; EnableKBSysReq; ScreenSwitchEnable; GetTableSeg, NewTable,
 * GetBIOSKeyProc (what they return, relative to the driver's segments);
 * WEP last. Buffers handed to the driver are the probe's own, filled with
 * CCh first: only what the driver writes into them is read back.
 */
#include <windows.h>

#define TK_REF "kb1ref.h"
#include "tklog.h"

extern DWORD __cdecl __far md_call(FARPROC f, WORD FAR *args, WORD FAR *out);
extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl get_ahincr(void);
extern WORD __cdecl get_0040h(void);
extern void __cdecl __far kb_callx(FARPROC f, WORD FAR *args, WORD FAR *out);
extern void __cdecl __far kb_event(void);

/* the protected-mode INT 9 vector (INT 21h AH=35h): v[0] selector, v[1]
   offset (kb1s.asm) */
extern void __cdecl __far get_vec9(WORD FAR *v);

static WORD args[16];
static WORD res[2];
static FARPROC p[138];
static BYTE ks[256];            /* key-state array */
static BYTE st[16];             /* ToAscii's state block */
static BYTE buf[600];
static BYTE buf2[600];
static char line[300];
static char lb[4096];           /* the log, written in blocks */
static WORD lbn;
static int popped_logged[138];
static WORD popped[138];

static void flush(void)
{
    if (lbn)
        log_write(lb, lbn);
    lbn = 0;
}

static void put(const char FAR *s)
{
    while (*s) {
        if (lbn == sizeof lb)
            flush();
        lb[lbn++] = *s++;
    }
}

static void clear(void)
{
    int i;
    for (i = 0; i < 16; i++)
        args[i] = 0;
}

static void fill(BYTE *b, WORD n, BYTE v)
{
    WORD i;
    for (i = 0; i < n; i++)
        b[i] = v;
}

/* call entry ord with args[] as set; log its stack use the first time */
static DWORD call(int ord)
{
    DWORD r;
    if (!p[ord]) {
        res[0] = 0xFFFF;
        return 0xFFFFFFFFL;
    }
    r = md_call(p[ord], args, res);
    if (!popped_logged[ord]) {          /* listed at the end */
        popped_logged[ord] = 1;
        popped[ord] = res[0];
    }
    return r;
}

static void hexline(const char *name, BYTE FAR *b, WORD n)
{
    WORD i, k;
    put(name);
    for (i = 0; i < n; i += 16) {
        k = 0;
        wsprintf(line, "\r\n  %02X:", i);
        put(line);
        for (k = 0; k < 16 && i + k < n; k++) {
            wsprintf(line, " %02X", b[i + k]);
            put(line);
        }
    }
    put("\r\n");
}

/* ---- ToAscii ---- */
/* the shift states: VKs held (0x80) or toggled (0x01) */
struct shst { const char *name; BYTE vk[3]; BYTE v[3]; };
static const struct shst states[] = {
    { "-",     { 0, 0, 0 },          { 0, 0, 0 } },
    { "S",     { VK_SHIFT, 0, 0 },   { 0x80, 0, 0 } },
    { "C",     { VK_CONTROL, 0, 0 }, { 0x80, 0, 0 } },
    { "SC",    { VK_SHIFT, VK_CONTROL, 0 }, { 0x80, 0x80, 0 } },
    { "A",     { VK_MENU, 0, 0 },    { 0x80, 0, 0 } },
    { "SA",    { VK_SHIFT, VK_MENU, 0 }, { 0x80, 0x80, 0 } },
    { "CA",    { VK_CONTROL, VK_MENU, 0 }, { 0x80, 0x80, 0 } },
    { "SCA",   { VK_SHIFT, VK_CONTROL, VK_MENU }, { 0x80, 0x80, 0x80 } },
    { "K",     { VK_CAPITAL, 0, 0 }, { 0x01, 0, 0 } },
    { "KS",    { VK_CAPITAL, VK_SHIFT, 0 }, { 0x01, 0x80, 0 } },
    { "KC",    { VK_CAPITAL, VK_CONTROL, 0 }, { 0x01, 0x80, 0 } },
    { "N",     { VK_NUMLOCK, 0, 0 }, { 0x01, 0, 0 } },
    { "NS",    { VK_NUMLOCK, VK_SHIFT, 0 }, { 0x01, 0x80, 0 } },
    { "Kd",    { VK_CAPITAL, 0, 0 }, { 0x80, 0, 0 } },
};
#define NSTATES (sizeof states / sizeof states[0])

static void setstate(const struct shst *s)
{
    int i;
    fill(ks, 256, 0);
    for (i = 0; i < 3; i++)
        if (s->vk[i])
            ks[s->vk[i]] = s->v[i];
}

/* ToAscii(vk, scan, ks, st, flags): "n:c1,c2" (the bytes written to the
   state block; CC = untouched) */
static int keepst;              /* the state block carried over */
static int toascii(WORD vk, WORD scan, WORD flags, char *o)
{
    DWORD r;
    int n;
    if (!keepst)
        fill(st, sizeof st, 0xCC);
    clear();
    args[9] = vk;
    args[10] = scan;
    args[11] = SELECTOROF((void FAR *)ks);
    args[12] = OFFSETOF((void FAR *)ks);
    args[13] = SELECTOROF((void FAR *)st);
    args[14] = OFFSETOF((void FAR *)st);
    args[15] = flags;
    r = call(4);
    n = (int)LOWORD(r);
    wsprintf(o, "%d:%02X%02X%02X%02X", n, st[0], st[1], st[2], st[3]);
    return n;
}

/* Alt released (the end of an Alt+keypad sequence): VK_MENU up */
static void altup(char *o)
{
    fill(ks, 256, 0);
    fill(st, sizeof st, 0);
    keepst = 1;
    toascii(VK_MENU, 0xC038, 0, o);
    keepst = 0;
}

static const BYTE padscan[10] = { 0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49 };
static const BYTE padvk_off[10] = { VK_INSERT, VK_END, VK_DOWN, VK_NEXT, VK_LEFT, VK_CLEAR,
                                    VK_RIGHT, VK_HOME, VK_UP, VK_PRIOR };

/* Alt held, the digits of s typed on the keypad (numlock on: VK_NUMPADn;
   off: the cursor VKs), Alt released; the state block (USER keeps one for
   all its calls) starts as init and is carried through */
static void altseq(const char *s, int numlock, BYTE init)
{
    char o[40];
    int i, d;
    wsprintf(line, "ALT %s %s %02X:", (LPSTR)s, (LPSTR)(numlock ? "num" : "nonum"), init);
    put(line);
    fill(st, sizeof st, init);
    keepst = 1;
    fill(ks, 256, 0);
    ks[VK_MENU] = 0x80;
    if (numlock)
        ks[VK_NUMLOCK] = 1;
    toascii(VK_MENU, 0x2038, 0, o);     /* Alt down */
    put(" ");
    put(o);
    for (i = 0; s[i]; i++) {
        d = s[i] - '0';
        ks[VK_MENU] = 0x80;
        toascii(numlock ? VK_NUMPAD0 + d : padvk_off[d], 0x2000 | padscan[d], 0, o);
        put(" ");
        put(o);
        toascii(numlock ? VK_NUMPAD0 + d : padvk_off[d], 0xE000 | padscan[d], 0, o);
        put(" ");
        put(o);
    }
    ks[VK_MENU] = 0;
    if (numlock)
        ks[VK_NUMLOCK] = 1;
    toascii(VK_MENU, 0xC038, 0, o);
    put(" up ");
    put(o);
    /* a key after it */
    fill(ks, 256, 0);
    toascii('B', 0x30, 0, o);
    put(" B ");
    put(o);
    put("\r\n");
    keepst = 0;
}

int __cdecl tmain(void)
{
    HMODULE hm = GetModuleHandle("KEYBOARD");
    DWORD r;
    WORD i, j, k, n;
    char o[40];
    U_INITAPP(init_regs[5]);
    out("KB1 start\r\n");
    obs("module", hm != 0);
    if (!hm)
        return 1;
    GetModuleFileName(hm, line, sizeof line);
    out("OBS ~file=\"");
    out(line);
    out("\"\r\n");
    for (i = 1; i < 138; i++) {
        p[i] = GetProcAddress(hm, MAKEINTRESOURCE(i));
        if (p[i]) {
            wsprintf(line, "OBS entry %u\r\n", i);
            put(line);
        }
    }
    wsprintf(line, "OBS entry WEP by name=%u\r\n", GetProcAddress(hm, "WEP") == p[8]);
    put(line);

    /* Inquire(LPKBINFO) */
    fill(buf, 32, 0xCC);
    clear();
    args[14] = SELECTOROF((void FAR *)buf);
    args[15] = OFFSETOF((void FAR *)buf);
    r = call(1);
    wsprintf(line, "OBS Inquire=%04X\r\n", LOWORD(r));
    put(line);
    hexline("OBS KBINFO", buf, 16);

    /* GetKeyboardType(n) */
    for (i = 0; i < 4; i++) {
        clear();
        args[15] = i;
        r = call(130);
        wsprintf(line, "OBS GetKeyboardType(%u)=%04X\r\n", i, LOWORD(r));
        put(line);
    }
    clear();
    r = call(132);
    wsprintf(line, "OBS GetKBCodePage=%04X\r\n", LOWORD(r));
    put(line);

    /* AnsiToOem / OemToAnsi: the string 1..255 */
    for (k = 5; k <= 6; k++) {
        for (i = 0; i < 255; i++)
            buf[i] = (BYTE)(i + 1);
        buf[255] = 0;
        fill(buf2, 260, 0xCC);
        clear();
        args[12] = SELECTOROF((void FAR *)buf);
        args[13] = OFFSETOF((void FAR *)buf);
        args[14] = SELECTOROF((void FAR *)buf2);
        args[15] = OFFSETOF((void FAR *)buf2);
        r = call(k);
        wsprintf(line, "OBS %s AX=%04X DX=~\r\n", (LPSTR)(k == 5 ? "AnsiToOem" : "OemToAnsi"), LOWORD(r));
        put(line);
        hexline(k == 5 ? "AnsiToOem" : "OemToAnsi", buf2, 258);
        /* in place */
        for (i = 0; i < 255; i++)
            buf[i] = (BYTE)(i + 1);
        buf[255] = 0;
        buf[256] = 0xCC;
        args[14] = SELECTOROF((void FAR *)buf);
        args[15] = OFFSETOF((void FAR *)buf);
        call(k);
        for (i = 0; i < 256 && buf[i] == buf2[i]; i++)
            ;
        wsprintf(line, "OBS %s in place same=%u\r\n", (LPSTR)(k == 5 ? "AnsiToOem" : "OemToAnsi"), i == 256);
        put(line);
        /* an empty string */
        buf[0] = 0;
        fill(buf2, 4, 0xCC);
        args[14] = SELECTOROF((void FAR *)buf2);
        args[15] = OFFSETOF((void FAR *)buf2);
        r = call(k);
        wsprintf(line, "OBS %s empty AX=%04X out=%02X %02X\r\n", (LPSTR)(k == 5 ? "AnsiToOem" : "OemToAnsi"),
                 LOWORD(r), buf2[0], buf2[1]);
        put(line);
    }
    /* the Buff forms: 0..255, count 256; count 0; in place */
    for (k = 134; k <= 135; k++) {
        for (i = 0; i < 256; i++)
            buf[i] = (BYTE)i;
        fill(buf2, 260, 0xCC);
        clear();
        args[11] = SELECTOROF((void FAR *)buf);
        args[12] = OFFSETOF((void FAR *)buf);
        args[13] = SELECTOROF((void FAR *)buf2);
        args[14] = OFFSETOF((void FAR *)buf2);
        args[15] = 256;
        r = call(k);
        wsprintf(line, "OBS %s AX=~ \r\n", (LPSTR)(k == 134 ? "AnsiToOemBuff" : "OemToAnsiBuff"));
        put(line);
        hexline(k == 134 ? "AnsiToOemBuff" : "OemToAnsiBuff", buf2, 258);
        fill(buf2, 8, 0xCC);
        args[15] = 0;
        call(k);
        wsprintf(line, "OBS %s count 0: %02X %02X\r\n", (LPSTR)(k == 134 ? "AnsiToOemBuff" : "OemToAnsiBuff"),
                 buf2[0], buf2[1]);
        put(line);
        args[13] = SELECTOROF((void FAR *)buf);
        args[14] = OFFSETOF((void FAR *)buf);
        args[15] = 256;
        fill(buf2, 8, 0xCC);
        args[13] = SELECTOROF((void FAR *)buf2);
        args[14] = OFFSETOF((void FAR *)buf2);
        call(k);
        for (i = 0; i < 256; i++)
            buf[i] = (BYTE)i;
        args[13] = SELECTOROF((void FAR *)buf);
        args[14] = OFFSETOF((void FAR *)buf);
        call(k);
        for (i = 0; i < 256 && buf[i] == buf2[i]; i++)
            ;
        wsprintf(line, "OBS %s in place same=%u\r\n", (LPSTR)(k == 134 ? "AnsiToOemBuff" : "OemToAnsiBuff"), i == 256);
        put(line);
    }
    /* across a 64K boundary: a string from offset FFF0h of a huge block
       into the next selector (+__AHINCR) */
    {
        HGLOBAL h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 0x20000L);
        HGLOBAL h2 = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 0x20000L);
        WORD inc = get_ahincr();
        WORD sa = SELECTOROF(GlobalLock(h)), sb = SELECTOROF(GlobalLock(h2));
        BYTE FAR *a0 = (BYTE FAR *)MAKELP(sa, 0), FAR *a1 = (BYTE FAR *)MAKELP(sa + inc, 0);
        BYTE FAR *b0 = (BYTE FAR *)MAKELP(sb, 0), FAR *b1 = (BYTE FAR *)MAKELP(sb + inc, 0);
        if (sa && sb) {
            for (i = 0xFFF0; i; i++)
                a0[i] = 0xE9;           /* e acute (ANSI) */
            for (i = 0; i < 0x20; i++)
                a1[i] = 0xE9;
            a1[0x20] = 0;
            for (k = 5; k <= 6; k++) {
                for (i = 0xFFE0; i; i++)
                    b0[i] = 0xCC;
                for (i = 0; i < 0x30; i++)
                    b1[i] = 0xCC;
                clear();
                args[12] = sa;
                args[13] = 0xFFF0;
                args[14] = sb;
                args[15] = 0xFFF0;
                call(k);
                wsprintf(line, "OBS huge %u: %02X %02X %02X %02X | %02X %02X %02X %02X\r\n", k,
                         b0[0xFFFE], b0[0xFFFF], b1[0], b1[1], b1[0x1F], b1[0x20], b1[0x21], b0[0xFFEF]);
                put(line);
            }
            for (k = 134; k <= 135; k++) {
                for (i = 0xFFE0; i; i++)
                    b0[i] = 0xCC;
                for (i = 0; i < 0x30; i++)
                    b1[i] = 0xCC;
                clear();
                args[11] = sa;
                args[12] = 0xFFF0;
                args[13] = sb;
                args[14] = 0xFFF0;
                args[15] = 0x20;
                call(k);
                wsprintf(line, "OBS huge %u: %02X %02X %02X %02X | %02X %02X %02X\r\n", k,
                         b0[0xFFFE], b0[0xFFFF], b1[0], b1[1], b1[0x0F], b1[0x10], b1[0x11]);
                put(line);
            }
        } else
            put("OBS huge: no memory\r\n");
        GlobalUnlock(h);
        GlobalUnlock(h2);
        GlobalFree(h);
        GlobalFree(h2);
    }

    /* VkKeyScan, OemKeyScan */
    for (i = 0; i < 256; i += 8) {
        wsprintf(line, "VKS %02X:", i);
        put(line);
        for (j = i; j < i + 8; j++) {
            clear();
            args[15] = j;
            r = call(129);
            wsprintf(line, " %04X", LOWORD(r));
            put(line);
        }
        put("\r\n");
    }
    for (i = 0; i < 256; i += 8) {
        wsprintf(line, "OKS %02X:", i);
        put(line);
        for (j = i; j < i + 8; j++) {
            clear();
            args[15] = j;
            r = call(128);
            wsprintf(line, " %04X.%04X", HIWORD(r), LOWORD(r));
            put(line);
        }
        put("\r\n");
    }
    /* high bytes of the argument */
    {
        static const WORD hi[] = { 0x141, 0xFF41, 0x8061, 0x1E1 };
        for (i = 0; i < 4; i++) {
            clear();
            args[15] = hi[i];
            r = call(129);
            wsprintf(line, "VKS %04X=%04X", hi[i], LOWORD(r));
            put(line);
            r = call(128);
            wsprintf(line, " OKS %04X=%04X.%04X\r\n", hi[i], HIWORD(r), LOWORD(r));
            put(line);
        }
    }

    /* MapVirtualKey(code, type) */
    for (k = 0; k < 5; k++)
        for (i = 0; i < 256; i += 16) {
            wsprintf(line, "MVK %u %02X:", k, i);
            put(line);
            for (j = i; j < i + 16; j++) {
                clear();
                args[14] = j;
                args[15] = k;
                r = call(131);
                wsprintf(line, " %X", LOWORD(r));
                put(line);
            }
            put("\r\n");
        }
    {
        static const WORD big[] = { 0x100, 0x11E, 0x141, 0x1C8, 0xE01E, 0xFFFF, 0x120, 0x130,
                                    0x1BA, 0x10D, 0x16A, 0x8041 };
        static const WORD types[] = { 0, 1, 2, 5, 0xFF, 0x100, 0x102, 0xFFFF };
        for (k = 0; k < 8; k++) {
            wsprintf(line, "MVK %X big:", types[k]);
            put(line);
            for (i = 0; i < 12; i++) {
                clear();
                args[14] = big[i];
                args[15] = types[k];
                r = call(131);
                wsprintf(line, " %X", LOWORD(r));
                put(line);
            }
            put("\r\n");
        }
    }

    /* GetKeyNameText(LONG lParam, LPSTR, int) */
    for (k = 0; k < 4; k++)
        for (i = 0; i < 256; i++) {
            DWORD lp = ((DWORD)i << 16) | ((DWORD)(k & 1) << 24) | ((DWORD)(k >> 1) << 25);
            fill(buf, 40, 0xCC);
            clear();
            args[11] = HIWORD(lp);
            args[12] = LOWORD(lp);
            args[13] = SELECTOROF((void FAR *)buf);
            args[14] = OFFSETOF((void FAR *)buf);
            args[15] = 32;
            r = call(133);
            for (j = 0; j < 32 && buf[j] != 0xCC; j++)
                ;
            buf[39] = 0;
            for (n = 0; n < 33 && buf[n]; n++)
                ;
            if (n > 32)
                n = 32;
            {
                char t[40];
                for (j = 0; j < n; j++)
                    t[j] = buf[j];
                t[n] = 0;
                wsprintf(line, "GKN %u %02X=%d \"%s\" end %02X %02X\r\n", k, i, (int)LOWORD(r), (LPSTR)t,
                         buf[n], buf[n + 1]);
            }
            put(line);
        }
    /* lParam's low word and other high bits; short buffers */
    {
        static const DWORD lps[] = { 0x001E0000L, 0x001EFFFFL, 0x401E0000L, 0x801E0000L, 0x041E0000L,
                                     0x011C0000L, 0x00390000L, 0x00450000L, 0x01450000L };
        static const WORD sizes[] = { 0, 1, 2, 3, 4, 5, 6, 32 };
        for (i = 0; i < 9; i++)
            for (k = 0; k < 8; k++) {
                if (i && k != 7 && lps[i] != 0x00390000L)
                    continue;
                fill(buf, 40, 0xCC);
                clear();
                args[11] = HIWORD(lps[i]);
                args[12] = LOWORD(lps[i]);
                args[13] = SELECTOROF((void FAR *)buf);
                args[14] = OFFSETOF((void FAR *)buf);
                args[15] = sizes[k];
                r = call(133);
                wsprintf(line, "GKN %08lX size %u=%d:", lps[i], sizes[k], (int)LOWORD(r));
                put(line);
                for (j = 0; j < 10; j++) {
                    wsprintf(line, " %02X", buf[j]);
                    put(line);
                }
                put("\r\n");
            }
    }
    flush();

    /* ToAscii: every VK in every shift state, scan code 0, key down */
    for (k = 0; k < NSTATES; k++) {
        for (i = 0; i < 256; i += 8) {
            wsprintf(line, "TA %s %02X:", (LPSTR)states[k].name, i);
            put(line);
            for (j = i; j < i + 8; j++) {
                setstate(&states[k]);
                toascii(j, 0, 0, o);
                put(" ");
                put(o);
                if (ks[VK_MENU]) {      /* anything Alt left pending */
                    char o2[40];
                    altup(o2);
                    if (o2[0] != '0') {
                        put("/");
                        put(o2);
                    }
                }
            }
            put("\r\n");
        }
        flush();
    }
    /* the real scan code (from the scan-code bits), key up, extended,
       KeyFlags bit 0 (a menu), Alt bit, for states - and S */
    {
        static const WORD var[] = { 0x8000, 0x4000, 0x0100, 0x2000, 0x1000, 0x0800 };
        for (k = 0; k < 2; k++)
            for (n = 0; n < 6; n++)
                for (i = 0; i < 256; i += 8) {
                    wsprintf(line, "TAV %u %04X %02X:", k, var[n], i);
                    put(line);
                    for (j = i; j < i + 8; j++) {
                        setstate(&states[k]);
                        toascii(j, var[n], 0, o);
                        put(" ");
                        put(o);
                    }
                    put("\r\n");
                }
        for (k = 0; k < 3; k++)
            for (i = 0; i < 256; i += 8) {
                wsprintf(line, "TAF %u %02X:", k, i);
                put(line);
                for (j = i; j < i + 8; j++) {
                    setstate(&states[k == 2 ? 4 : k]);
                    toascii(j, 0, k == 2 ? 1 : (k ? 2 : 1), o);
                    put(" ");
                    put(o);
                    if (ks[VK_MENU]) {
                        char o2[40];
                        altup(o2);
                        if (o2[0] != '0') {
                            put("/");
                            put(o2);
                        }
                    }
                }
                put("\r\n");
            }
        flush();
    }
    /* scan codes for the letters: does ToAscii look at them? (VK_A with
       the scan code of every key) */
    put("TAS:");
    for (i = 0; i < 128; i++) {
        fill(ks, 256, 0);
        toascii('A', i, 0, o);
        put(" ");
        put(o);
    }
    put("\r\n");
    put("TAS num:");
    for (i = 0x47; i < 0x54; i++) {
        fill(ks, 256, 0);
        ks[VK_NUMLOCK] = 1;
        toascii(VK_NUMPAD5, i, 0, o);
        put(" ");
        put(o);
        toascii(VK_DECIMAL, i, 0, o);
        put(" ");
        put(o);
        toascii(VK_DELETE, i | 0x100, 0, o);
        put(" ");
        put(o);
    }
    put("\r\n");
    flush();

    /* Alt+keypad */
    {
        static const char *seqs[] = { "65", "065", "0065", "0233", "130", "176", "0128", "0", "300",
                                      "256", "1234", "00", "9", "255", "0255", "127", "0127", "32",
                                      "1", "01", "" };
        for (i = 0; i < sizeof seqs / sizeof seqs[0]; i++) {
            altseq(seqs[i], 1, 0);
            altseq(seqs[i], 0, 0);
        }
        altseq("65", 1, 0xCC);
        altseq("", 1, 0xCC);
        altseq("065", 1, 0xCC);
        /* Alt+keypad interrupted by another key */
        fill(st, sizeof st, 0);
        keepst = 1;
        fill(ks, 256, 0);
        ks[VK_MENU] = 0x80;
        ks[VK_NUMLOCK] = 1;
        put("ALT 6 x 5:");
        toascii(VK_MENU, 0x2038, 0, o); put(" "); put(o);
        toascii(VK_NUMPAD6, 0x204D, 0, o); put(" "); put(o);
        toascii('X', 0x202D, 0, o); put(" "); put(o);
        toascii(VK_NUMPAD5, 0x204C, 0, o); put(" "); put(o);
        ks[VK_MENU] = 0;
        toascii(VK_MENU, 0xC038, 0, o); put(" up "); put(o);
        put("\r\n");
        /* digits without Alt, then Alt up */
        fill(ks, 256, 0);
        ks[VK_NUMLOCK] = 1;
        put("NOALT 65:");
        toascii(VK_NUMPAD6, 0x004D, 0, o); put(" "); put(o);
        toascii(VK_NUMPAD5, 0x004C, 0, o); put(" "); put(o);
        toascii(VK_MENU, 0xC038, 0, o); put(" up "); put(o);
        put("\r\n");
        /* Ctrl+Alt+keypad */
        fill(ks, 256, 0);
        ks[VK_MENU] = 0x80;
        ks[VK_CONTROL] = 0x80;
        ks[VK_NUMLOCK] = 1;
        put("CTRLALT 65:");
        toascii(VK_NUMPAD6, 0x204D, 0, o); put(" "); put(o);
        toascii(VK_NUMPAD5, 0x204C, 0, o); put(" "); put(o);
        ks[VK_MENU] = 0;
        toascii(VK_MENU, 0xC038, 0, o); put(" up "); put(o);
        ks[VK_CONTROL] = 0;
        altup(o); put(" up2 "); put(o);
        put("\r\n");
        /* Alt, a digit, a key-up of another digit */
        fill(ks, 256, 0);
        ks[VK_MENU] = 0x80;
        ks[VK_NUMLOCK] = 1;
        fill(st, sizeof st, 0);
        keepst = 1;
        put("ALTUPS:");
        toascii(VK_MENU, 0x2038, 0, o); put(" "); put(o);
        toascii(VK_NUMPAD6, 0x204D, 0, o); put(" "); put(o);
        toascii(VK_NUMPAD5, 0xE04C, 0, o); put(" "); put(o);
        toascii(VK_MENU, 0x6038, 0, o); put(" "); put(o);  /* Alt repeat */
        toascii(VK_NUMPAD5, 0x204C, 0, o); put(" "); put(o);
        toascii(VK_SHIFT, 0x202A, 0, o); put(" "); put(o);
        toascii(VK_NUMPAD5, 0x204C, 0, o); put(" "); put(o);
        ks[VK_MENU] = 0;
        toascii(VK_MENU, 0xC038, 0, o); put(" up "); put(o);
        toascii(VK_MENU, 0xC038, 0, o); put(" up "); put(o);
        put("\r\n");
        keepst = 0;
        flush();
    }

    /* Alt up (and other keys) with given state blocks: which bytes are
       written */
    {
        static const BYTE init[][4] = { { 0xCC, 0xCC, 0xCC, 0xCC }, { 0x41, 0x00, 0xCC, 0xCC },
                                        { 0x41, 0xCC, 0xCC, 0xCC }, { 0xCC, 0x00, 0xCC, 0xCC },
                                        { 0x82, 0x01, 0xCC, 0xCC }, { 0x00, 0x41, 0xCC, 0xCC },
                                        { 0x00, 0x00, 0xCC, 0xCC }, { 0x82, 0x00, 0x01, 0xCC } };
        static const WORD ev[][3] = { { VK_MENU, 0xC038, 0 }, { VK_MENU, 0x8038, 0 },
                                      { VK_MENU, 0x0038, 0 }, { VK_MENU, 0x2038, 0 },
                                      { VK_MENU, 0x6038, 0 }, { VK_NUMPAD5, 0x204C, 1 },
                                      { VK_NUMPAD5, 0x004C, 1 }, { VK_NUMPAD5, 0x204C, 2 },
                                      { VK_SHIFT, 0x202A, 1 }, { VK_F1, 0x203B, 1 },
                                      { 'A', 0x201E, 1 }, { VK_CONTROL, 0xC01D, 0 },
                                      { VK_SHIFT, 0xC02A, 0 } };
        for (j = 0; j < sizeof ev / sizeof ev[0]; j++) {
            wsprintf(line, "STB %02X %04X %u:", ev[j][0], ev[j][1], ev[j][2]);
            put(line);
            for (i = 0; i < sizeof init / sizeof init[0]; i++) {
                fill(ks, 256, 0);
                if (ev[j][2] & 1)
                    ks[VK_MENU] = 0x80;
                if (ev[j][2] & 2)
                    ks[VK_CONTROL] = 0x80;
                ks[VK_NUMLOCK] = 1;
                for (k = 0; k < 4; k++)
                    st[k] = init[i][k];
                keepst = 1;
                toascii(ev[j][0], ev[j][1], 0, o);
                keepst = 0;
                put(" ");
                put(o);
            }
            put("\r\n");
        }
        /* sequences: Alt down, digit, KEY, digit, Alt up */
        {
            static const WORD mid[][2] = { { VK_SHIFT, 0x202A }, { VK_F1, 0x203B }, { 'X', 0x202D },
                                           { VK_SHIFT, 0xE02A }, { VK_CONTROL, 0x201D },
                                           { VK_MENU, 0x6038 }, { VK_CAPITAL, 0x203A },
                                           { VK_NUMPAD5, 0xE04C }, { VK_RETURN, 0x201C },
                                           { VK_SPACE, 0x2039 }, { VK_BACK, 0x200E } };
            for (j = 0; j < sizeof mid / sizeof mid[0]; j++) {
                wsprintf(line, "SEQ %02X %04X:", mid[j][0], mid[j][1]);
                put(line);
                fill(st, sizeof st, 0);
                keepst = 1;
                fill(ks, 256, 0);
                ks[VK_MENU] = 0x80;
                ks[VK_NUMLOCK] = 1;
                toascii(VK_MENU, 0x2038, 0, o); put(" "); put(o);
                toascii(VK_NUMPAD6, 0x204D, 0, o); put(" "); put(o);
                toascii(VK_NUMPAD6, 0xE04D, 0, o); put(" "); put(o);
                toascii(mid[j][0], mid[j][1], 0, o); put(" "); put(o);
                toascii(VK_NUMPAD5, 0x204C, 0, o); put(" "); put(o);
                toascii(VK_NUMPAD5, 0xE04C, 0, o); put(" "); put(o);
                ks[VK_MENU] = 0;
                toascii(VK_MENU, 0xC038, 0, o); put(" up "); put(o);
                put("\r\n");
                keepst = 0;
            }
        }
        flush();
    }

    /* VK 0: the lights from the key-state array */
    fill(ks, 256, 0);
    r = toascii(0, 0, 0, o);
    wsprintf(line, "OBS ToAscii(0)=%s\r\n", (LPSTR)o);
    put(line);

    /* SetSpeed */
    {
        static const int sp[] = { -1, 31, 0, 15, 32, 0x60, 0x7F, -2, 0x1F };
        for (i = 0; i < sizeof sp / sizeof sp[0]; i++) {
            clear();
            args[15] = sp[i];
            r = call(7);
            wsprintf(line, "OBS SetSpeed(%d)=%04X\r\n", sp[i], LOWORD(r));
            put(line);
        }
    }
    /* EnableKBSysReq */
    {
        static const WORD sr[] = { 0, 1, 0, 2, 4, 0, 8, 5, 10, 0x10, 0xFFFF, 0, 10 };
        for (i = 0; i < sizeof sr / sizeof sr[0]; i++) {
            clear();
            args[15] = sr[i];
            r = call(136);
            wsprintf(line, "OBS EnableKBSysReq(%X)=%04X\r\n", sr[i], LOWORD(r));
            put(line);
        }
    }
    /* ScreenSwitchEnable */
    clear();
    args[15] = 0;
    r = call(100);
    clear();
    args[15] = 1;
    r = call(100);
    put("OBS ScreenSwitchEnable done\r\n");

    /* GetTableSeg, NewTable, GetBIOSKeyProc: what they return, relative
       to the driver's own segments (which entries' selectors match) */
    {
        WORD t;
        clear();
        r = call(126);
        t = LOWORD(r);
        wsprintf(line, "OBS ~GetTableSeg=%04X:%04X\r\n", HIWORD(r), LOWORD(r));
        put(line);
        put("OBS GetTableSeg AX is the selector of entries");
        for (i = 1; i < 138; i++)
            if (p[i] && SELECTOROF(p[i]) == t) {
                wsprintf(line, " %u", i);
                put(line);
            }
        put("\r\n");
        clear();
        r = call(126);
        wsprintf(line, "OBS GetTableSeg again same=%u\r\n", LOWORD(r) == t);
        put(line);
        clear();
        r = call(127);
        put("OBS NewTable done\r\n");
        clear();
        r = call(126);
        wsprintf(line, "OBS GetTableSeg after NewTable same=%u\r\n", LOWORD(r) == t);
        put(line);
        clear();
        r = call(137);
        wsprintf(line, "OBS ~GetBIOSKeyProc=%04X:%04X\r\n", HIWORD(r), LOWORD(r));
        put(line);
        put("OBS GetBIOSKeyProc DX is the selector of entries");
        for (i = 1; i < 138; i++)
            if (p[i] && SELECTOROF(p[i]) == HIWORD(r)) {
                wsprintf(line, " %u", i);
                put(line);
            }
        put("\r\n");
        for (i = 1; i < 138; i++)
            if (p[i] && p[i] == (FARPROC)r) {
                wsprintf(line, "OBS GetBIOSKeyProc is entry %u\r\n", i);
                put(line);
            }
    }
    /* the translations once more after NewTable */
    for (i = 0; i < 256; i += 16) {
        wsprintf(line, "MVK2 %02X:", i);
        put(line);
        for (j = i; j < i + 16; j++) {
            clear();
            args[14] = j;
            args[15] = 1;
            r = call(131);
            wsprintf(line, " %X", LOWORD(r));
            put(line);
        }
        put("\r\n");
    }
    fill(ks, 256, 0);
    toascii('A', 0x1E, 0, o);
    wsprintf(line, "OBS ToAscii A after NewTable=%s\r\n", (LPSTR)o);
    put(line);
    flush();

    /* registers: every entry with markers in AX-DI (kb1s.asm); SI, DI,
       DS and BP must come back (the Pascal convention), AX/DX are the
       result where there is one. Arguments as above (harmless ones). */
    {
        static const WORD ords[] = { 1, 4, 5, 6, 7, 100, 126, 127, 128, 129, 130, 131, 132, 133,
                                     134, 135, 136, 137 };
        WORD rr[17];
        for (k = 0; k < sizeof ords / sizeof ords[0]; k++) {
            WORD o2 = ords[k];
            clear();
            buf[0] = 'a';
            buf[1] = 0;
            fill(st, sizeof st, 0);
            fill(ks, 256, 0);
            switch (o2) {
            case 1:
                args[14] = SELECTOROF((void FAR *)buf2);
                args[15] = OFFSETOF((void FAR *)buf2);
                break;
            case 4:
                args[9] = 'A';
                args[10] = 0x1E;
                args[11] = SELECTOROF((void FAR *)ks);
                args[12] = OFFSETOF((void FAR *)ks);
                args[13] = SELECTOROF((void FAR *)st);
                args[14] = OFFSETOF((void FAR *)st);
                break;
            case 5: case 6:
                args[12] = SELECTOROF((void FAR *)buf);
                args[13] = OFFSETOF((void FAR *)buf);
                args[14] = SELECTOROF((void FAR *)buf2);
                args[15] = OFFSETOF((void FAR *)buf2);
                break;
            case 134: case 135:
                args[11] = SELECTOROF((void FAR *)buf);
                args[12] = OFFSETOF((void FAR *)buf);
                args[13] = SELECTOROF((void FAR *)buf2);
                args[14] = OFFSETOF((void FAR *)buf2);
                args[15] = 2;
                break;
            case 7: args[15] = 31; break;
            case 100: args[15] = 1; break;
            case 128: case 129: args[15] = 'a'; break;
            case 130: args[15] = 0; break;
            case 131: args[14] = 'A'; break;
            case 133:
                args[11] = 0x1E;
                args[13] = SELECTOROF((void FAR *)buf2);
                args[14] = OFFSETOF((void FAR *)buf2);
                args[15] = 16;
                break;
            case 136: args[15] = 0; break;
            }
            kb_callx(p[o2], args, rr);
            /* AX is the result except for the void ones (100, 127, 134,
               135); DX only for the DWORD ones (128, 137); BX, CX, ES
               are the callee's to use (layout, "~") */
            {
                int vax = o2 == 100 || o2 == 127 || o2 == 134 || o2 == 135;
                int vdx = o2 == 128 || o2 == 137;
                wsprintf(line, "OBS regs %u: popped=%u %sAX=%04X %sDX=%04X keeps SI=%u DI=%u DS=%u BP=%u"
                         " ~ES=%u ~BX=%04X ~CX=%04X\r\n",
                         o2, rr[0], (LPSTR)(vax ? "~" : ""), rr[2], (LPSTR)(vdx ? "" : "~"), rr[5],
                         rr[6] == 0x5555, rr[7] == 0x6666,
                         rr[8] == SELECTOROF((void FAR *)rr), rr[10] == OFFSETOF((void FAR *)rr),
                         rr[9] == SELECTOROF((void FAR *)rr), rr[3], rr[4]);
            }
            put(line);
        }
    }
    flush();

    /* Disable and Enable: the INT 9 vector (INT 21h AH=35h) before and
       after, GetBIOSKeyProc against it; Enable with KB1's own (do-nothing)
       event procedure and key-state array: what Enable writes into the
       array. Windows' own keyboard input is gone after this. */
    {
        WORD v9s[2], v9o[2], g[2];
        WORD rr[17];
        get_vec9(v9s);
        clear();
        r = call(137);
        g[0] = HIWORD(r);
        g[1] = LOWORD(r);
        wsprintf(line, "OBS vec9 enabled is an entry's segment=%u, GetBIOSKeyProc=vec9 %u\r\n",
                 0, g[0] == v9s[0] && g[1] == v9s[1]);
        put(line);
        for (i = 1; i < 138; i++)
            if (p[i] && SELECTOROF(p[i]) == v9s[0]) {
                wsprintf(line, "OBS vec9 enabled: in the segment of entry %u\r\n", i);
                put(line);
                break;
            }
        clear();
        kb_callx(p[3], args, rr);
        wsprintf(line, "OBS regs 3: popped=%u keeps SI=%u DI=%u DS=%u BP=%u\r\n", rr[0],
                 rr[6] == 0x5555, rr[7] == 0x6666, rr[8] == SELECTOROF((void FAR *)rr),
                 rr[10] == OFFSETOF((void FAR *)rr));
        put(line);
        get_vec9(v9o);
        wsprintf(line, "OBS vec9 disabled changed=%u, = GetBIOSKeyProc %u\r\n",
                 v9o[0] != v9s[0] || v9o[1] != v9s[1], g[0] == v9o[0] && g[1] == v9o[1]);
        put(line);
        clear();
        r = call(137);
        wsprintf(line, "OBS GetBIOSKeyProc disabled same=%u\r\n", HIWORD(r) == g[0] && LOWORD(r) == g[1]);
        put(line);
        clear();
        kb_callx(p[3], args, rr);
        put("OBS Disable twice done\r\n");
        fill(ks, 256, 0xCC);
        clear();
        args[12] = SELECTOROF((FARPROC)kb_event);
        args[13] = OFFSETOF((FARPROC)kb_event);
        args[14] = SELECTOROF((void FAR *)ks);
        args[15] = OFFSETOF((void FAR *)ks);
        kb_callx(p[2], args, rr);
        wsprintf(line, "OBS regs 2: popped=%u keeps SI=%u DI=%u DS=%u BP=%u\r\n", rr[0],
                 rr[6] == 0x5555, rr[7] == 0x6666, rr[8] == SELECTOROF((void FAR *)rr),
                 rr[10] == OFFSETOF((void FAR *)rr));
        put(line);
        {
            BYTE FAR *bda = (BYTE FAR *)MAKELP(get_0040h(), 0);
            wsprintf(line, "OBS BDA 17=%02X 18=%02X 96=%02X 97=%02X\r\n", bda[0x17], bda[0x18],
                     bda[0x96], bda[0x97]);
            put(line);
        }
        put("OBS Enable wrote:");
        for (i = 0; i < 256; i++)
            if (ks[i] != 0xCC) {
                wsprintf(line, " %02X=%02X", i, ks[i]);
                put(line);
            }
        put("\r\n");
        get_vec9(v9o);
        wsprintf(line, "OBS vec9 enabled again same=%u\r\n", v9o[0] == v9s[0] && v9o[1] == v9s[1]);
        put(line);
        /* Enable while enabled: the array again */
        fill(ks, 256, 0xCC);
        kb_callx(p[2], args, rr);
        put("OBS Enable twice wrote:");
        for (i = 0; i < 256; i++)
            if (ks[i] != 0xCC) {
                wsprintf(line, " %02X=%02X", i, ks[i]);
                put(line);
            }
        put("\r\n");
        /* Enable with BIOS keyboard flags set: what it puts in the array,
           then the flags back as they were */
        {
            BYTE FAR *bda = (BYTE FAR *)MAKELP(get_0040h(), 0);
            static const BYTE f17[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x7F };
            static const BYTE f18[] = { 0x01, 0x02, 0x04, 0x08 };
            BYTE o17 = bda[0x17], o18 = bda[0x18], o96 = bda[0x96];
            for (k = 0; k < sizeof f17 + sizeof f18 + 1; k++) {
                clear();
                kb_callx(p[3], args, rr);
                if (k < sizeof f17) {
                    bda[0x17] = f17[k];
                    wsprintf(line, "OBS Enable with 40:17=%02X wrote:", f17[k]);
                } else if (k < sizeof f17 + sizeof f18) {
                    bda[0x18] = f18[k - sizeof f17];
                    wsprintf(line, "OBS Enable with 40:18=%02X wrote:", f18[k - sizeof f17]);
                } else {
                    bda[0x96] = 0x02;          /* the E0 prefix seen */
                    wsprintf(line, "OBS Enable with 40:96=02 wrote:");
                }
                put(line);
                fill(ks, 256, 0xCC);
                clear();
                args[12] = SELECTOROF((FARPROC)kb_event);
                args[13] = OFFSETOF((FARPROC)kb_event);
                args[14] = SELECTOROF((void FAR *)ks);
                args[15] = OFFSETOF((void FAR *)ks);
                kb_callx(p[2], args, rr);
                for (i = 0; i < 256; i++)
                    if (ks[i] != 0xCC) {
                        wsprintf(line, " %02X=%02X", i, ks[i]);
                        put(line);
                    }
                wsprintf(line, "; then 40:17=%02X 18=%02X 96=%02X\r\n", bda[0x17], bda[0x18], bda[0x96]);
                put(line);
                bda[0x17] = o17;
                bda[0x18] = o18;
                bda[0x96] = o96;
            }
            flush();
        }
    }
    flush();

    /* WEP last (it might turn the keyboard off); AX on entry 1111h */
    {
        WORD rr[17];
        clear();
        args[15] = 0;
        kb_callx(p[8], args, rr);
        wsprintf(line, "OBS WEP(0) ~AX=%04X popped=%u\r\n", rr[2], rr[0]);
        put(line);
    }
    for (i = 1; i < 138; i++)
        if (popped_logged[i]) {
            wsprintf(line, "OBS popped %u=%u\r\n", i, popped[i]);
            put(line);
        }
    put("KB1 done\r\n");
    flush();
    return 0;
}
