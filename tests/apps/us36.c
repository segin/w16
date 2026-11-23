/*
 * us36.c - probe US36: text past 32767 characters; counts and positions
 * of 32768 or more (signed or unsigned?).
 *
 * Run as the shell on Windows 3.1 (tests/dropin.sh -r) and with w16's
 * USER (-f USER.EXE); the OBS lines of the two logs must be identical
 * (tests/boottest.sh case edit32k). Each edit control's text is in a 64K
 * segment of its own made a local heap (LocalInit; its selector the
 * control's hInstance, as Notepad's), so it can hold 60000 characters.
 * Sections:
 *   ml.L     a multi-line control (ES_AUTOHSCROLL | ES_AUTOVSCROLL, no
 *            wrapping) given L characters (64-character lines, CR LF)
 *            with SetWindowText: GetWindowTextLength, WM_GETTEXTLENGTH,
 *            and GetWindowText and WM_GETTEXT with counts 0 to 65535:
 *            the answer, where the NUL went, the first character not the
 *            text's, the byte after the NUL (55h untouched)
 *   sel      EM_SETSEL in the 51193-character control from a selection
 *            10-20: the answer, EM_GETSEL's two words, the first visible
 *            line, for starts and ends around 32767, past the text, and
 *            the -1 (FFFFh) and -2 conventions; wParam 1
 *   lim      EM_LIMITTEXT's answers; EM_REPLACESEL and WM_CHAR past
 *            32767 under the default limit (30000), under 40000 and
 *            under none (0)
 *   line     EM_GETLINECOUNT, EM_LINEFROMCHAR, EM_LINEINDEX,
 *            EM_LINELENGTH and EM_GETLINE around 32767 and with FFFFh,
 *            FFFEh and 8000h
 *   edit     EM_REPLACESEL, typing, Delete, Backspace and EM_UNDO past
 *            32767 (Notepad's Find and type: NP11)
 *   wb       Ctrl+Right and Ctrl+Left past 32767, without and with a
 *            word-break procedure (its last call's arguments)
 *   sl       a single-line control given 40000 characters (it keeps
 *            32766): counts, EM_SETSEL, EM_GETSEL, EM_LINELENGTH,
 *            EM_LIMITTEXT, EM_REPLACESEL and typing at its maximum
 *   dw       DefWindowProc's text (a static control, a button, the
 *            main window), a combo box's: GetWindowText and WM_GETTEXT
 *            with counts of 32768 or more, a drop-down list's cut;
 *            WM_SETTEXT's answer and texts of 2000 to 33000 characters
 */
#include <windows.h>

#define TK_REF "us36ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define EM_GETTHUMB (WM_USER + 14)

static HINSTANCE hi;
static HWND hMain;
static HGLOBAL gsrc, gdst;
static LPSTR src, dst;          /* 64K each */
static char nm[64];

static void pump(DWORD ms)
{
    MSG m;
    DWORD t = GetTickCount();
    do {
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    } while (GetTickCount() - t < ms);
}

static void o(const char *pfx, const char *what, WORD v)
{
    wsprintf(nm, "%s.%s", (LPCSTR)pfx, (LPCSTR)what);
    obs(nm, v);
}

/* src: L characters, lines of ll then CR LF (ll 0: no line ends), NUL */
static void gen(WORD L, WORD ll)
{
    WORD i, c = 0, k = 0;
    for (i = 0; i < L; i++) {
        if (ll && c == ll) {
            src[i] = '\r';
            c++;
        } else if (ll && c == ll + 1) {
            src[i] = '\n';
            c = 0;
            k++;
        } else {
            src[i] = (char)('a' + (c + k) % 26);
            c++;
        }
    }
    src[L] = 0;
}

static void fill_dst(void)
{
    WORD i = 0;
    do
        dst[i] = 0x55;
    while (++i != 0);
}

/* GetWindowText (msg 0) or WM_GETTEXT (1) with count n, compared with s */
static void gt(const char *pfx, HWND h, WORD n, int msg, LPCSTR s)
{
    char p[40];
    WORD r, i, nul = 0xFFFF, bad = 0xFFFF;
    fill_dst();
    r = msg ? (WORD)SendMessage(h, WM_GETTEXT, n, (LPARAM)dst) : (WORD)GetWindowText(h, dst, (int)n);
    for (i = 0;; i++) {
        if (dst[i] == 0) {
            nul = i;
            break;
        }
        if (i == 0xFFFF)
            break;
    }
    for (i = 0; nul != 0xFFFF && i < nul; i++)
        if (dst[i] != s[i]) {
            bad = i;
            break;
        }
    wsprintf(p, "%s.%c%04X", (LPCSTR)pfx, msg ? 'm' : 'g', n);
    o(p, "r", r);
    o(p, "nul", nul);
    o(p, "bad", bad);
    o(p, "after", nul == 0xFFFF ? 0xFFFF : (BYTE)dst[nul + 1]);
    if (n == 0)
        o(p, "b0", (BYTE)dst[0]);
}

static const WORD counts[] = {
    0, 1, 2, 100, 32766, 32767, 32768, 32769, 40000, 51192, 51193, 51194,
    60000, 60001, 65534, 65535
};

static void gt_all(const char *pfx, HWND h, LPCSTR s)
{
    int i;
    for (i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        gt(pfx, h, counts[i], 0, s);
        gt(pfx, h, counts[i], 1, s);
    }
}

/* a segment of 64K made a local heap; its selector */
static WORD heap_seg(HGLOBAL *g)
{
    LPSTR p;
    *g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0x10000L);
    p = *g ? (LPSTR)GlobalLock(*g) : NULL;
    if (!p || !LocalInit(SELECTOROF(p), 16, 0xFFFE))
        return 0;
    return SELECTOROF(p);
}

static HWND mkedit(DWORD style, HGLOBAL *g)
{
    WORD sel = heap_seg(g);
    if (!sel)
        return 0;
    return CreateWindow("Edit", "", WS_CHILD | WS_VISIBLE | WS_BORDER | style,
                        10, 10, 300, 200, hMain, (HMENU)100, (HINSTANCE)sel, NULL);
}

static void kill(HWND e, HGLOBAL g)
{
    DestroyWindow(e);
    GlobalUnlock(g);
    GlobalFree(g);
}

#define MLS (ES_MULTILINE | ES_AUTOHSCROLL | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL)

/* a few characters of the text at p as one word each, from GetWindowText */
static void around(const char *pfx, HWND e, WORD p, WORD n)
{
    char w[16];
    WORD i, len = (WORD)GetWindowTextLength(e);
    fill_dst();
    GetWindowText(e, dst, (int)(len + 1));
    for (i = 0; i < n; i++) {
        wsprintf(w, "c%u", p + i);
        o(pfx, w, p + i <= len ? (BYTE)dst[p + i] : 0xFFFF);
    }
}

static void sel_state(const char *pfx, HWND e)
{
    DWORD s = SendMessage(e, EM_GETSEL, 0, 0);
    o(pfx, "s0", LOWORD(s));
    o(pfx, "s1", HIWORD(s));
    o(pfx, "len", (WORD)GetWindowTextLength(e));
}

static const WORD selpairs[][2] = {
    { 0, 0 }, { 32767, 32767 }, { 32768, 32768 }, { 32767, 32769 },
    { 40000, 40010 }, { 51190, 51192 }, { 51193, 51193 }, { 51194, 51194 },
    { 60000, 60000 }, { 0xFFFF, 0xFFFF }, { 0xFFFF, 5 }, { 5, 0xFFFF },
    { 0, 0xFFFF }, { 0xFFFE, 0xFFFE }, { 0x8000, 0xFFFF }, { 40010, 40000 },
    { 0xFFFF, 0 }, { 32767, 0 }, { 0x7FFF, 0x8000 }, { 0xFFFE, 0 },
    { 0, 0xFFFE }, { 1, 0x8000 }, { 0x8000, 1 }, { 0xFFFF, 40000 },
    { 40000, 0xFFFF }, { 0xFFFF, 0x8000 }, { 30, 0xFFFF }, { 0xFFFF, 30 },
    { 0, 0 }
};

static void sec_sel(HWND e, WORD L, const char *pfx)
{
    char p[40];
    int i;
    for (i = 0; i == 0 || selpairs[i][0] || selpairs[i][1]; i++) {
        SendMessage(e, EM_SETSEL, 0, MAKELONG(10, 20));
        SendMessage(e, EM_SETSEL, 0, MAKELONG(0, 0));
        SendMessage(e, EM_SETSEL, 0, MAKELONG(10, 20));
        wsprintf(p, "%s.%04X.%04X", (LPCSTR)pfx, selpairs[i][0], selpairs[i][1]);
        o(p, "r", (WORD)SendMessage(e, EM_SETSEL, 0, MAKELONG(selpairs[i][0], selpairs[i][1])));
        sel_state(p, e);
        o(p, "top", (WORD)SendMessage(e, EM_GETFIRSTVISIBLELINE, 0, 0));
        o(p, "lfc", (WORD)SendMessage(e, EM_LINEFROMCHAR, 0xFFFF, 0));
    }
    /* the caret's end: from a reversed selection, a char typed */
    (void)L;
    SendMessage(e, EM_SETSEL, 0, MAKELONG(0, 0));
    wsprintf(p, "%s.np", (LPCSTR)pfx);
    o(p, "r", (WORD)SendMessage(e, EM_SETSEL, 1, MAKELONG(45000, 45000)));
    sel_state(p, e);
    o(p, "top", (WORD)SendMessage(e, EM_GETFIRSTVISIBLELINE, 0, 0));
}

static void sec_lim(HWND e, const char *pfx)
{
    static const WORD lv[] = { 1, 30000, 32767, 32768, 40000, 65535, 0 };
    char p[40];
    int i;
    for (i = 0; i < 7; i++) {
        wsprintf(p, "%s.l%04X", (LPCSTR)pfx, lv[i]);
        o(p, "r", (WORD)SendMessage(e, EM_LIMITTEXT, lv[i], 0));
    }
}

static void sec_line(HWND e, const char *pfx)
{
    static const WORD lfc[] = { 0, 32767, 32768, 40000, 51192, 51193, 51194, 60000,
                                0x8000, 0xFFFE, 0xFFFF };
    static const WORD li[] = { 0, 100, 496, 497, 774, 775, 776, 777, 0x7FFF, 0x8000,
                               0xFFFE, 0xFFFF };
    static const WORD ll[] = { 0, 32767, 32768, 40000, 51193, 51194, 0x8000, 0xFFFE, 0xFFFF };
    char p[40], w[16];
    WORD lc, i, r, ln;
    lc = (WORD)SendMessage(e, EM_GETLINECOUNT, 0, 0);
    o(pfx, "lc", lc);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(40000, 40000));
    for (i = 0; i < sizeof lfc / sizeof lfc[0]; i++) {
        wsprintf(w, "lfc%04X", lfc[i]);
        o(pfx, w, (WORD)SendMessage(e, EM_LINEFROMCHAR, lfc[i], 0));
    }
    for (i = 0; i < sizeof li / sizeof li[0]; i++) {
        wsprintf(w, "li%04X", li[i]);
        o(pfx, w, (WORD)SendMessage(e, EM_LINEINDEX, li[i], 0));
    }
    for (i = 0; i < sizeof ll / sizeof ll[0]; i++) {
        wsprintf(w, "ll%04X", ll[i]);
        o(pfx, w, (WORD)SendMessage(e, EM_LINELENGTH, ll[i], 0));
    }
    /* -1 with a selection over lines */
    SendMessage(e, EM_SETSEL, 0, MAKELONG(39990, 40100));
    o(pfx, "llsel", (WORD)SendMessage(e, EM_LINELENGTH, 0xFFFF, 0));
    o(pfx, "lisel", (WORD)SendMessage(e, EM_LINEINDEX, 0xFFFF, 0));
    o(pfx, "lfcsel", (WORD)SendMessage(e, EM_LINEFROMCHAR, 0xFFFF, 0));
    /* EM_GETLINE */
    ln = (WORD)SendMessage(e, EM_LINEFROMCHAR, 40000, 0);
    {
        static const WORD gl[] = { 0, 1, 0x7FFF, 0x8000, 0xFFFF };
        int k;
        for (k = 0; k < 6; k++) {
            WORD line = k == 5 ? ln : gl[k], bad = 0xFFFF, st;
            fill_dst();
            *(WORD FAR *)dst = 200;
            r = (WORD)SendMessage(e, EM_GETLINE, line, (LPARAM)dst);
            st = (WORD)SendMessage(e, EM_LINEINDEX, line, 0);
            wsprintf(p, "%s.gl%04X", (LPCSTR)pfx, k == 5 ? 0xAAAA : line);
            o(p, "r", r);
            if (r && r < 300 && st != 0xFFFF) {
                for (i = 0; i < r; i++)
                    if (dst[i] != src[st + i]) {
                        bad = i;
                        break;
                    }
            }
            o(p, "bad", bad);
            o(p, "after", (BYTE)dst[r]);
        }
    }
}

static void type(HWND e, char c)
{
    SendMessage(e, WM_CHAR, (BYTE)c, 1);
}

static void key(HWND e, WORD vk)
{
    SendMessage(e, WM_KEYDOWN, vk, 1);
    SendMessage(e, WM_KEYUP, vk, 0xC0000001L);
}

static void step(const char *pfx, const char *what, HWND e, WORD at)
{
    char p[40];
    wsprintf(p, "%s.%s", (LPCSTR)pfx, (LPCSTR)what);
    sel_state(p, e);
    o(p, "mod", (WORD)SendMessage(e, EM_GETMODIFY, 0, 0));
    o(p, "undo", (WORD)SendMessage(e, EM_CANUNDO, 0, 0));
    around(p, e, at, 6);
}

/* changes past 32767 under the limit lim (0: the default) */
static void sec_edit(HWND e, const char *pfx, WORD lim)
{
    if (lim)
        SendMessage(e, EM_LIMITTEXT, lim == 1 ? 0 : lim, 0);
    SetFocus(e);
    SendMessage(e, EM_SETMODIFY, 0, 0);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(40000, 40005));
    SendMessage(e, EM_REPLACESEL, 0, (LPARAM)(LPCSTR)"ABC");
    step(pfx, "rs", e, 39998);
    SendMessage(e, EM_UNDO, 0, 0);
    step(pfx, "un", e, 39998);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(51190, 51192));
    type(e, 'Q');
    step(pfx, "ty", e, 51188);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(45000, 45000));
    key(e, VK_DELETE);
    step(pfx, "del", e, 44998);
    type(e, 8);
    step(pfx, "bs", e, 44996);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(33000, 33000));
    type(e, 'x');
    type(e, 'y');
    step(pfx, "xy", e, 32998);
    key(e, VK_END);
    step(pfx, "end", e, 33060);
    key(e, VK_DOWN);
    step(pfx, "down", e, 33120);
    pump(10);
}

static void sec_ml(WORD L)
{
    HGLOBAL g;
    HWND e = mkedit(MLS, &g);
    char p[16];
    wsprintf(p, "ml%u", L);
    o(p, "made", e != 0);
    if (!e)
        return;
    gen(L, 64);
    SetWindowText(e, src);
    o(p, "len", (WORD)GetWindowTextLength(e));
    o(p, "wmlen", (WORD)SendMessage(e, WM_GETTEXTLENGTH, 0, 0));
    o(p, "lc", (WORD)SendMessage(e, EM_GETLINECOUNT, 0, 0));
    gt_all(p, e, src);
    if (L == 51193) {
        out("sel\r\n");
        sec_sel(e, L, "sel");
        out("lim\r\n");
        sec_lim(e, "lim");
        out("line\r\n");
        sec_line(e, "line");
    }
    kill(e, g);
}

static void sec_edits(void)
{
    static const WORD lims[] = { 0, 40000, 1 };
    int i;
    for (i = 0; i < 3; i++) {
        HGLOBAL g;
        HWND e = mkedit(MLS, &g);
        char p[16];
        wsprintf(p, "ed%u", i);
        o(p, "made", e != 0);
        if (!e)
            continue;
        gen(51193, 64);
        SetWindowText(e, src);
        sec_edit(e, p, lims[i]);
        kill(e, g);
    }
}

/* ---- moving by words past 32767, with and without a word-break
   procedure ---- */
static WORD wb_ich, wb_cch, wb_code, wb_calls;

int FAR PASCAL __export __loadds wbproc(LPSTR t, int ich, int cch, int code)
{
    WORD i = (WORD)ich, n = (WORD)cch;
    wb_ich = i;
    wb_cch = n;
    wb_code = (WORD)code;
    wb_calls++;
    if (code == WB_ISDELIMITER)
        return i < n && t[i] == ' ';
    if (code == WB_LEFT) {
        while (i > 0 && t[i - 1] == ' ')
            i--;
        while (i > 0 && t[i - 1] != ' ')
            i--;
        return (int)i;
    }
    while (i < n && t[i] != ' ')
        i++;
    while (i < n && t[i] == ' ')
        i++;
    return (int)i;
}

static void ctrl_key(HWND e, WORD vk)
{
    BYTE ks[256];
    GetKeyboardState(ks);
    ks[VK_CONTROL] = 0x80;
    SetKeyboardState(ks);
    key(e, vk);
    ks[VK_CONTROL] = 0;
    SetKeyboardState(ks);
}

static void sec_wb(void)
{
    static const WORD at[] = { 40000, 40003, 32766, 51000 };
    HGLOBAL g;
    HWND e = mkedit(MLS, &g);
    FARPROC thunk = MakeProcInstance((FARPROC)wbproc, hi);
    WORD i, k;
    char p[40];
    o("wb", "made", e != 0);
    if (!e)
        return;
    for (i = 0; i < 51193; i++)
        src[i] = (i % 66) == 64 ? '\r' : (i % 66) == 65 ? '\n' : (i % 9) == 8 ? ' ' : (char)('a' + i % 9);
    src[51193] = 0;
    SetWindowText(e, src);
    SetFocus(e);
    for (k = 0; k < 2; k++) {
        if (k)
            SendMessage(e, EM_SETWORDBREAKPROC, 0, (LPARAM)thunk);
        for (i = 0; i < 4; i++) {
            wsprintf(p, "wb%u.%u", k, at[i]);
            SendMessage(e, EM_SETSEL, 0, MAKELONG(at[i], at[i]));
            wb_calls = 0;
            ctrl_key(e, VK_RIGHT);
            sel_state(p, e);
            o(p, "rcalls", wb_calls);
            o(p, "rich", wb_ich);
            o(p, "rcch", wb_cch);
            o(p, "rcode", wb_code);
            SendMessage(e, EM_SETSEL, 0, MAKELONG(at[i], at[i]));
            wb_calls = 0;
            ctrl_key(e, VK_LEFT);
            wsprintf(p, "wb%u.%u.l", k, at[i]);
            sel_state(p, e);
            o(p, "lcalls", wb_calls);
            o(p, "lich", wb_ich);
            o(p, "lcode", wb_code);
        }
    }
    SendMessage(e, EM_SETWORDBREAKPROC, 0, 0);
    kill(e, g);
    FreeProcInstance(thunk);
}

static void sec_sl(void)
{
    HGLOBAL g;
    HWND e = mkedit(ES_AUTOHSCROLL, &g);
    char p[40];
    static const WORD pairs[][2] = {
        { 32767, 32768 }, { 32768, 32768 }, { 39990, 40000 }, { 40001, 40001 },
        { 0xFFFF, 0xFFFF }, { 0, 0xFFFF }, { 0xFFFF, 5 }, { 0x8000, 0xFFFE }
    };
    int i;
    o("sl", "made", e != 0);
    if (!e)
        return;
    gen(40000, 0);
    SetWindowText(e, src);
    o("sl", "len", (WORD)GetWindowTextLength(e));
    gt("sl", e, 32768, 0, src);
    gt("sl", e, 40000, 1, src);
    gt("sl", e, 40001, 0, src);
    gt("sl", e, 65535, 1, src);
    for (i = 0; i < 8; i++) {
        SendMessage(e, EM_SETSEL, 0, MAKELONG(10, 20));
        wsprintf(p, "sl.%04X.%04X", pairs[i][0], pairs[i][1]);
        o(p, "r", (WORD)SendMessage(e, EM_SETSEL, 0, MAKELONG(pairs[i][0], pairs[i][1])));
        sel_state(p, e);
    }
    o("sl", "ll", (WORD)SendMessage(e, EM_LINELENGTH, 35000, 0));
    o("sl", "llm1", (WORD)SendMessage(e, EM_LINELENGTH, 0xFFFF, 0));
    o("sl", "lfc", (WORD)SendMessage(e, EM_LINEFROMCHAR, 35000, 0));
    o("sl", "li", (WORD)SendMessage(e, EM_LINEINDEX, 0xFFFF, 0));
    o("sl", "lim0", (WORD)SendMessage(e, EM_LIMITTEXT, 0, 0));
    SendMessage(e, EM_SETSEL, 0, MAKELONG(35000, 35002));
    SendMessage(e, EM_REPLACESEL, 0, (LPARAM)(LPCSTR)"Z");
    step("sl", "rs", e, 34998);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(100, 105));
    SendMessage(e, EM_REPLACESEL, 0, (LPARAM)(LPCSTR)"Z");
    step("sl", "rs2", e, 98);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(200, 200));
    SendMessage(e, EM_REPLACESEL, 0, (LPARAM)(LPCSTR)"ABCDEFGH");
    step("sl", "rs3", e, 198);
    SetFocus(e);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(300, 300));
    type(e, 'k');
    type(e, 'l');
    step("sl", "ty", e, 298);
    o("sl", "lim9C40", (WORD)SendMessage(e, EM_LIMITTEXT, 40000, 0));
    SendMessage(e, EM_SETSEL, 0, MAKELONG(400, 400));
    type(e, 'k');
    step("sl", "ty2", e, 398);
    kill(e, g);
    /* SetWindowText's cut, below and at it */
    {
        static const WORD sz[] = { 32765, 32766, 32767 };
        for (i = 0; i < 3; i++) {
            e = mkedit(ES_AUTOHSCROLL, &g);
            gen(sz[i], 0);
            SetWindowText(e, src);
            wsprintf(p, "sl%u", sz[i]);
            o(p, "len", (WORD)GetWindowTextLength(e));
            kill(e, g);
        }
    }
}

static void sec_dw(void)
{
    static const WORD dc[] = { 0x7FFF, 0x8000, 0x8001, 40000, 0xFFFF };
    HWND st = CreateWindow("Static", "Static text", WS_CHILD | WS_VISIBLE | SS_LEFT,
                           10, 220, 200, 20, hMain, (HMENU)101, hi, NULL);
    HWND bt = CreateWindow("Button", "Push", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           220, 220, 60, 20, hMain, (HMENU)102, hi, NULL);
    HWND cl = CreateWindow("ComboBox", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                           300, 220, 100, 80, hMain, (HMENU)103, hi, NULL);
    HWND cd = CreateWindow("ComboBox", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN,
                           410, 220, 80, 80, hMain, (HMENU)104, hi, NULL);
    int i;
    SendMessage(cl, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)"Item one");
    SendMessage(cl, CB_SETCURSEL, 0, 0);
    SetWindowText(cd, "Combo edit");
    for (i = 0; i < 5; i++) {
        gt("st", st, dc[i], 0, "Static text");
        gt("st", st, dc[i], 1, "Static text");
        gt("bt", bt, dc[i], 0, "Push");
        gt("mw", hMain, dc[i], 0, "US36");
        gt("mw", hMain, dc[i], 1, "US36");
        gt("cl", cl, dc[i], 0, "Item one");
        gt("cl", cl, dc[i], 1, "Item one");
        gt("cd", cd, dc[i], 0, "Combo edit");
        gt("cd", cd, dc[i], 1, "Combo edit");
    }
    gt("st", st, 3, 0, "Static text");
    gt("st", st, 0, 0, "Static text");
    /* a drop-down list's cut and long counts in detail */
    {
        static const WORD cc[] = { 5, 8, 9, 0x7FFF, 0x8000, 0xFFFF };
        int k;
        for (i = 0; i < 6; i++) {
            char w[16], b[8];
            fill_dst();
            wsprintf(w, "cld.%04X", cc[i]);
            o(w, "r", (WORD)SendMessage(cl, WM_GETTEXT, cc[i], (LPARAM)dst));
            for (k = 0; k < 12; k++) {
                wsprintf(b, "b%d", k);
                o(w, b, (BYTE)dst[k]);
            }
            /* (past 32767 3.1 copies what follows the item's text in
               its heap) */
            if (cc[i] < 0x8000)
                o(w, "wm2", (BYTE)dst[cc[i] - 2]);
            else
                obs_env("cld.wm2", (BYTE)dst[cc[i] - 2]);
            o(w, "wm1", (BYTE)dst[cc[i] - 1]);
            o(w, "w", (BYTE)dst[cc[i]]);
        }
    }
    /* long texts in USER's heap */
    {
        static const WORD sz[] = { 2000, 10000, 20000, 30000, 32000, 32766, 32767, 32768 };
        for (i = 0; i < 8; i++) {
            char w[16];
            gen(sz[i], 0);
            wsprintf(w, "st%u", sz[i]);
            o(w, "r", (WORD)SendMessage(st, WM_SETTEXT, 0, (LPARAM)src));
            o(w, "len", (WORD)GetWindowTextLength(st));
            SetWindowText(st, "x");
        }
        /* DefWindowProc's own (the main window) and a button's; the
           text before kept? */
        for (i = 0; i < 4; i++) {
            char w[16];
            HWND t = i < 2 ? hMain : bt;
            gen(i & 1 ? 32768 : 32767, 0);
            wsprintf(w, "%s%u", (LPCSTR)(i < 2 ? "mw" : "bt"), i & 1 ? 32768 : 32767);
            SetWindowText(t, "abc");
            o(w, "r", (WORD)SendMessage(t, WM_SETTEXT, 0, (LPARAM)src));
            o(w, "len", (WORD)GetWindowTextLength(t));
            o(w, "r0", (WORD)SendMessage(t, WM_SETTEXT, 0, (LPARAM)(LPCSTR)"abc"));
        }
        SetWindowText(hMain, "US36");
    }
    /* a long text in USER's heap */
    gen(33000, 0);
    SetWindowText(st, src);
    o("stl", "len", (WORD)GetWindowTextLength(st));
    gt("stl", st, 0x8000, 0, src);
    gt("stl", st, 33001, 0, src);
    gt("stl", st, 0xFFFF, 1, src);
    SetWindowText(st, "x");
    DestroyWindow(st);
    DestroyWindow(bt);
    DestroyWindow(cl);
    DestroyWindow(cd);
}

LRESULT FAR PASCAL __export __loadds main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProc(h, m, w, l);
}

int __cdecl tmain(void)
{
    static const WORD sizes[] = { 100, 32766, 32767, 32768, 32769, 40000, 51193, 60000 };
    WNDCLASS wc;
    int i;
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US36 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)main_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US36M";
    RegisterClass(&wc);
    hMain = CreateWindow("US36M", "US36", WS_OVERLAPPEDWINDOW, 0, 0, 520, 300, 0, 0, hi, NULL);
    ShowWindow(hMain, SW_SHOWNORMAL);
    UpdateWindow(hMain);
    pump(300);
    gsrc = GlobalAlloc(GMEM_MOVEABLE, 0x10000L);
    gdst = GlobalAlloc(GMEM_MOVEABLE, 0x10000L);
    src = (LPSTR)GlobalLock(gsrc);
    dst = (LPSTR)GlobalLock(gdst);
    if (!src || !dst) {
        out("US36 no memory\r\n");
        return 1;
    }
    for (i = 0; i < 8; i++) {
        out("ml\r\n");
        sec_ml(sizes[i]);
    }
    out("edit\r\n");
    sec_edits();
    out("wb\r\n");
    sec_wb();
    out("sl\r\n");
    sec_sl();
    out("dw\r\n");
    sec_dw();
    out("US36 end\r\n");
    GlobalUnlock(gsrc);
    GlobalUnlock(gdst);
    GlobalFree(gsrc);
    GlobalFree(gdst);
    DestroyWindow(hMain);
    return 0;
}
