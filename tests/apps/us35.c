/*
 * us35.c - probe US35: a multi-line edit control's word wrap and undo.
 *
 * Run as the shell on Windows 3.1 (tests/dropin.sh -r) and with w16's
 * USER (-f USER.EXE); the OBS lines of the two logs must be identical
 * (tests/boottest.sh case editwrap). Sections:
 *   w       edit controls (WS_BORDER | ES_MULTILINE, no ES_AUTOHSCROLL)
 *           80, 130 and 200 pixels wide in three fonts (system, MS Sans
 *           Serif 8 bold, SYSTEM_FIXED_FONT), given nine texts (spaces,
 *           tabs, long words, CR LF, lone CR and LF, CR CR LF, hyphens):
 *           EM_GETLINECOUNT, each line's EM_LINEINDEX, EM_LINELENGTH and
 *           EM_GETLINE, EM_LINEFROMCHAR at every position, the answers
 *           past the end; at 130 also EM_FMTLINES TRUE and FALSE (the
 *           text after each) and a checksum of the pixels
 *   edge    the system fixed font in widths 80 to 100 one pixel at a
 *           time: where the lines break (the format rectangle's width)
 *   rect    EM_GETRECT of several styles; EM_SETRECT, EM_SETRECTNP and a
 *           resize after them
 *   s*      scripts (see run_script): keys and the caret over wrapped
 *           lines, scrolling, resizing and WM_SETFONT, the word-break
 *           procedure (every call it gets: action, position, count),
 *           ES_CENTER and ES_RIGHT, the text limit, and undo: what each
 *           EM_UNDO, WM_UNDO, Ctrl+Z, Alt+Backspace reverts and when the
 *           buffer empties; the clipboard keys; each step logs the text,
 *           the selection, the caret, the first line, EM_CANUNDO,
 *           EM_GETMODIFY and the parent's EN_ notifications
 */
#include <windows.h>

#define TK_REF "us35ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern void __cdecl call_keybd_event(WORD ax, WORD bx);
extern void __cdecl call_mouse_event(WORD status, WORD x, WORD y, WORD buttons);

#ifndef EM_SETWORDBREAKPROC
#define EM_SETWORDBREAKPROC (WM_USER + 32)
#define EM_GETWORDBREAKPROC (WM_USER + 33)
#endif
#define EM_GETTHUMB (WM_USER + 14)

static HINSTANCE hi;
static HWND hMain, ed;
static HFONT fnt[4];
static char nrec[200];          /* the parent's EN_ notifications */
static char wrec[1600];         /* the word-break procedure's calls */
static char line[2400];
static int wbmode;              /* 0 spaces, 1 hyphens, 2 nothing */
/* the procedures' instance thunks (MakeProcInstance: an exported
   function of a program loads its DS from AX, which the thunk sets; a
   raw pointer hung 3.1 and w16 alike, whatever AX was) */
static FARPROC wbthunk, wboldthunk;

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

static void add(char *buf, int size, const char FAR *t)
{
    int n = lstrlen(buf);
    if (n + lstrlen(t) + 1 < size)
        lstrcpy(buf + n, t);
}

/* text made printable: \r \n \t as ^M ^J ^I */
static void esc(char *d, int dmax, const char FAR *s, int n)
{
    int i, k = 0;
    for (i = 0; i < n && k < dmax - 3; i++) {
        char c = s[i];
        if (c == '\r' || c == '\n' || c == '\t' || c == 0) {
            d[k++] = '^';
            d[k++] = c == '\r' ? 'M' : c == '\n' ? 'J' : c == '\t' ? 'I' : '@';
        } else
            d[k++] = c;
    }
    d[k] = 0;
}

/* ---- pixels ---- */
typedef struct { BITMAPINFOHEADER h; RGBQUAD pal[16]; } BMI16;
static BMI16 bmi;

static WORD grab(RECT *a)
{
    int cx = a->right - a->left, cy = a->bottom - a->top, x, y;
    WORD stride = (WORD)(((cx * 4 + 31) / 32) * 4);
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy), old;
    HGLOBAL hb = GlobalAlloc(GMEM_MOVEABLE, (DWORD)(WORD)(stride * cy));
    BYTE FAR *buf = (BYTE FAR *)GlobalLock(hb);
    WORD s = 0;
    old = SelectObject(mem, bm);
    BitBlt(mem, 0, 0, cx, cy, scr, a->left, a->top, SRCCOPY);
    SelectObject(mem, old);
    bmi.h.biSize = sizeof bmi.h;
    bmi.h.biWidth = cx;
    bmi.h.biHeight = cy;
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = 4;
    bmi.h.biCompression = 0;
    bmi.h.biSizeImage = 0;
    GetDIBits(scr, bm, 0, cy, buf, (BITMAPINFO FAR *)&bmi, DIB_RGB_COLORS);
    for (y = 0; y < cy; y++) {
        BYTE FAR *row = buf + (WORD)(cy - 1 - y) * stride;
        for (x = 0; x < cx; x++) {
            int p = (x & 1) ? row[x >> 1] & 15 : row[x >> 1] >> 4;
            s = (WORD)(s * 31 + p + 1);
        }
    }
    GlobalUnlock(hb);
    GlobalFree(hb);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
    return s;
}

/* the screen saved as C:\US35_nn.BMP (for looking at) */
static int shot;
static void snap(void)
{
    static char bmpname[16] = "C:\\US35_00.BMP";
    int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN), y;
    WORD stride = (WORD)(((cx * 4 + 31) / 32) * 4);
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy), old;
    HGLOBAL hb = GlobalAlloc(GMEM_MOVEABLE, (DWORD)stride * 32);
    BYTE FAR *buf = (BYTE FAR *)GlobalLock(hb);
    BITMAPFILEHEADER fh;
    HFILE f;
    shot++;
    bmpname[8] = (char)('0' + shot / 10);
    bmpname[9] = (char)('0' + shot % 10);
    old = SelectObject(mem, bm);
    BitBlt(mem, 0, 0, cx, cy, scr, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    bmi.h.biSize = sizeof bmi.h;
    bmi.h.biWidth = cx;
    bmi.h.biHeight = cy;
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = 4;
    bmi.h.biCompression = 0;
    bmi.h.biSizeImage = 0;
    for (y = 0; y < cy; y++)
        bmi.h.biSizeImage += stride;
    f = _lcreat(bmpname, 0);
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof bmi;
    fh.bfSize = fh.bfOffBits + bmi.h.biSizeImage;
    fh.bfReserved1 = fh.bfReserved2 = 0;
    _llseek(f, fh.bfOffBits, 0);
    for (y = 0; y < cy; y += 32) {
        GetDIBits(scr, bm, y, 32, buf, (BITMAPINFO FAR *)&bmi, DIB_RGB_COLORS);
        _lwrite(f, buf, stride * 32);
    }
    _llseek(f, 0, 0);
    _lwrite(f, (LPCSTR)&fh, sizeof fh);
    _lwrite(f, (LPCSTR)&bmi, sizeof bmi);
    _lclose(f);
    GlobalUnlock(hb);
    GlobalFree(hb);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
}

static WORD grab_win(HWND h)
{
    RECT r;
    UpdateWindow(h);
    GetWindowRect(h, &r);
    return grab(&r);
}

/* ---- the edit control ---- */
static HWND mk(DWORD style, int w, int hgt, int f)
{
    HWND h = CreateWindow("Edit", "", WS_CHILD | WS_VISIBLE | style, 10, 10, w, hgt,
                          hMain, (HMENU)100, hi, NULL);
    if (h && fnt[f])
        SendMessage(h, WM_SETFONT, (WPARAM)fnt[f], 0);
    return h;
}

static void dump_lines(const char *nm, HWND e)
{
    char g[260], t[600], name[48];
    int lc = (int)SendMessage(e, EM_GETLINECOUNT, 0, 0), i, p, prev = -1;
    int len = GetWindowTextLength(e);
    wsprintf(line, "lc=%d len=%d", lc, len);
    for (i = 0; i < lc && i < 80; i++) {
        int st = (int)SendMessage(e, EM_LINEINDEX, i, 0);
        int ll = (int)SendMessage(e, EM_LINELENGTH, st, 0);
        wsprintf(t, " %d+%d", st, ll);
        add(line, sizeof line, t);
    }
    wsprintf(t, " | li(lc)=%d ll(len)=%d lfc(len)=%d lfc(len+3)=%d li(-1)=%d ll(-1)=%d lfc(-1)=%d",
             (int)SendMessage(e, EM_LINEINDEX, lc, 0), (int)SendMessage(e, EM_LINELENGTH, len, 0),
             (int)SendMessage(e, EM_LINEFROMCHAR, len, 0), (int)SendMessage(e, EM_LINEFROMCHAR, len + 3, 0),
             (int)SendMessage(e, EM_LINEINDEX, (WPARAM)-1, 0), (int)SendMessage(e, EM_LINELENGTH, (WPARAM)-1, 0),
             (int)SendMessage(e, EM_LINEFROMCHAR, (WPARAM)-1, 0));
    add(line, sizeof line, t);
    wsprintf(name, "%s.l", (LPSTR)nm);
    obs_str(name, line);
    line[0] = 0;
    for (i = 0; i < lc && i < 80; i++) {
        int n;
        *(WORD *)g = 200;
        n = (int)SendMessage(e, EM_GETLINE, i, (LPARAM)(LPSTR)g);
        if (n < 0 || n > 200)
            n = 0;
        esc(t, sizeof t - 2, g, n);
        add(line, sizeof line, "[");
        add(line, sizeof line, t);
        add(line, sizeof line, "]");
    }
    wsprintf(name, "%s.g", (LPSTR)nm);
    obs_str(name, line);
    line[0] = 0;
    for (p = 0; p <= len && p < 1000; p++) {
        int l = (int)SendMessage(e, EM_LINEFROMCHAR, p, 0);
        if (l != prev) {
            wsprintf(t, " %d:%d", p, l);
            add(line, sizeof line, t);
            prev = l;
        }
    }
    wsprintf(name, "%s.f", (LPSTR)nm);
    obs_str(name, line);
}

static void log_text(const char *nm, HWND e)
{
    static char buf[1200];
    int n = GetWindowText(e, buf, sizeof buf);
    esc(line, sizeof line, buf, n);
    obs_str(nm, line);
}

/* a trace closed after every line, so that it survives a hang
   (C:\US35WB.LOG; DOSBox keeps TK.LOG's last writes until it exits) */
static void trace(const char *t)
{
    HFILE f = _lopen("C:\\US35WB.LOG", OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\US35WB.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    _lwrite(f, t, lstrlen(t));
    _lwrite(f, "\r\n", 2);
    _lclose(f);
}

/* ---- the word-break procedure: its calls logged ---- */
static int isdelim(char c)
{
    if (wbmode == 0)
        return c == ' ';
    if (wbmode == 1)
        return c == '-' || c == ' ';
    return 0;
}

int FAR PASCAL __export __loadds wbproc(LPSTR p, int ich, int cch, int code)
{
    char t[60];
    int i = ich, r;
    /* the arguments traced before anything is read through p */
    wsprintf(t, "call %d %d %d %04X:%04X", code, ich, cch, SELECTOROF(p), OFFSETOF(p));
    trace(t);
    if (IsBadReadPtr(p, cch > 0 ? cch : 1) || ich < 0 || ich > cch) {
        trace("bad");
        add(wrec, sizeof wrec, " bad");
        return code == WB_ISDELIMITER ? 0 : ich;
    }
    if (code == WB_LEFT) {
        while (i > 0 && isdelim(p[i - 1]))
            i--;
        while (i > 0 && !isdelim(p[i - 1]))
            i--;
        r = i;
    } else if (code == WB_RIGHT) {
        while (i < cch && !isdelim(p[i]))
            i++;
        while (i < cch && isdelim(p[i]))
            i++;
        r = i;
    } else
        r = ich >= 0 && ich < cch ? isdelim(p[ich]) : 0;
    wsprintf(t, " %d:%d/%d'%c%c'=%d", code, ich, cch, p[0] ? p[0] : '@',
             ich >= 0 && ich < cch && p[ich] >= ' ' ? p[ich] : '$', r);
    add(wrec, sizeof wrec, t);
    trace(t);
    return r;
}

/* a 3.0-style procedure for EM_SETWORDBREAK (WM_USER+26): logged only */
int FAR PASCAL __export __loadds wbold(LPSTR p, int ich, int cch, int code)
{
    char t[40];
    wsprintf(t, " old%d:%d/%d", code, ich, cch);
    add(wrec, sizeof wrec, t);
    (void)p;
    return 0;
}

/* ---- scripts ----
   Tokens, separated by spaces (in text '~' is a space, '|' CR LF
   (typed: Enter), '`' a lone CR, '/' a lone LF):
     sA,B  EM_SETSEL(0, A,B)      SA,B  EM_SETSEL(1, A,B)
     tTXT  WM_CHAR each           b     WM_CHAR 8      XN  WM_CHAR N
     kK    a real key: D U L R H E P N (PgUp PgDn) d(Del) B(Backspace)
           I(Insert) A(letter a); prefixes ^ Ctrl, + Shift, ! Alt
     u EM_UNDO  w WM_UNDO  z EM_CANUNDO  E EM_EMPTYUNDOBUFFER
     p WM_PASTE  x WM_CUT  c WM_COPY  k WM_CLEAR  C the clipboard's text
     RTXT EM_REPLACESEL   TTXT WM_SETTEXT   FN EM_FMTLINES N
     H EM_SETHANDLE (a new block "handle")   M EM_SETMODIFY 0
     LN EM_LIMITTEXT N    @X,Y a click     #X,Y a double click
     vC[,P] WM_VSCROLL    hC[,P] WM_HSCROLL    yN,M EM_LINESCROLL
     g WM_VSCROLL EM_GETTHUMB    WN the width N    GN WM_SETFONT fnt[N]
     D the lines (dump_lines)    Q the pixels' checksum
     O the old text shown (Notepad's toggle)  P EM_GETRECT
     Y EM_SETWORDBREAKPROC wbproc   Z EM_SETWORDBREAKPROC 0
     J EM_GETWORDBREAKPROC == wbproc, 0, else  V EM_SETWORDBREAK wbold
     mN word-break mode N    rN EM_SETREADONLY N */
static int num(const char **s)
{
    int v = 0, neg = 0;
    if (**s == '-') {
        neg = 1;
        (*s)++;
    }
    while (**s >= '0' && **s <= '9')
        v = v * 10 + *(*s)++ - '0';
    if (**s == ',')
        (*s)++;
    return neg ? -v : v;
}

static void txt(char *d, const char *s, int max)
{
    int k = 0;
    while (*s && *s != ' ' && k < max - 1) {
        if (*s == '|' && k < max - 2) {
            d[k++] = '\r';
            d[k++] = '\n';
        } else
            d[k++] = *s == '~' ? ' ' : *s == '`' ? '\r' : *s == '/' ? '\n' : *s;
        s++;
    }
    d[k] = 0;
}

static void realkey(char k, int mods)
{
    static const struct { char c; BYTE vk, scan; } keys[] = {
        { 'D', VK_DOWN, 0x50 }, { 'U', VK_UP, 0x48 }, { 'L', VK_LEFT, 0x4B }, { 'R', VK_RIGHT, 0x4D },
        { 'H', VK_HOME, 0x47 }, { 'E', VK_END, 0x4F }, { 'P', VK_PRIOR, 0x49 }, { 'N', VK_NEXT, 0x51 },
        { 'd', VK_DELETE, 0x53 }, { 'B', VK_BACK, 0x0E }, { 'I', VK_INSERT, 0x52 }, { 'A', 'A', 0x1E },
        { 'Z', 'Z', 0x2C }, { 'X', 'X', 0x2D }, { 'C', 'C', 0x2E }, { 'V', 'V', 0x2F }, { 0, 0, 0 }
    };
    int i;
    for (i = 0; keys[i].c && keys[i].c != k; i++)
        ;
    if (!keys[i].c)
        return;
    if (mods & 1)
        call_keybd_event(VK_CONTROL, 0x1D);
    if (mods & 2)
        call_keybd_event(VK_SHIFT, 0x2A);
    if (mods & 4)
        call_keybd_event(VK_MENU, 0x38);
    call_keybd_event(keys[i].vk, keys[i].scan);
    call_keybd_event(0x8000 | keys[i].vk, keys[i].scan);
    if (mods & 4)
        call_keybd_event(0x8000 | VK_MENU, 0x38);
    if (mods & 2)
        call_keybd_event(0x8000 | VK_SHIFT, 0x2A);
    if (mods & 1)
        call_keybd_event(0x8000 | VK_CONTROL, 0x1D);
}

static void set_clip(const char *s)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, lstrlen(s) + 1);
    lstrcpy((LPSTR)GlobalLock(g), s);
    GlobalUnlock(g);
    OpenClipboard(hMain);
    EmptyClipboard();
    SetClipboardData(CF_TEXT, g);
    CloseClipboard();
}

static void get_clip(char *d, int max)
{
    HANDLE g;
    d[0] = 0;
    if (!OpenClipboard(hMain))
        return;
    g = GetClipboardData(CF_TEXT);
    if (g) {
        LPSTR p = GlobalLock(g);
        if (p) {
            esc(d, max, p, lstrlen(p));
            GlobalUnlock(g);
        }
    }
    CloseClipboard();
}

static void click(HWND e, int x, int y, int dbl)
{
    LPARAM l = MAKELONG(x, y);
    SendMessage(e, WM_LBUTTONDOWN, MK_LBUTTON, l);
    SendMessage(e, WM_LBUTTONUP, 0, l);
    if (dbl) {
        SendMessage(e, WM_LBUTTONDBLCLK, MK_LBUTTON, l);
        SendMessage(e, WM_LBUTTONUP, 0, l);
    }
}

typedef struct {
    const char *name;
    DWORD style;
    int w, h, font;
    const char *text;
    const char *script;
} SCRIPT;

static void run_script(const SCRIPT *sc)
{
    const char *s = sc->script;
    char tok[200], name[48], t[300], tt[220];
    int step = 0;
    static char buf[700];
    ed = mk(sc->style, sc->w, sc->h, sc->font);
    if (!ed) {
        obs_str(sc->name, "no edit");
        return;
    }
    if (sc->text) {
        txt(buf, sc->text, sizeof buf);
        SetWindowText(ed, buf);
    }
    set_clip("PASTE");
    SetFocus(ed);
    pump(50);
    nrec[0] = 0;
    wrec[0] = 0;
    while (*s) {
        const char *a;
        LONG ret = 0;
        int mods = 0, n;
        DWORD sel;
        POINT cp;
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        for (n = 0; s[n] && s[n] != ' ' && n < (int)sizeof tok - 1; n++)
            tok[n] = s[n];
        tok[n] = 0;
        s += n;
        a = tok;
        while (*a == '^' || *a == '+' || *a == '!') {
            mods |= *a == '^' ? 1 : *a == '+' ? 2 : 4;
            a++;
        }
        step++;
        wsprintf(name, "%s.%d", (LPSTR)sc->name, step);
        if (sc->name[0] == 'w' && sc->name[1] == 'b') {
            wsprintf(t, "step %s %s", (LPSTR)name, (LPSTR)tok);
            trace(t);
        }
        switch (*a++) {
        case 's': { int x = num(&a), y = num(&a); ret = SendMessage(ed, EM_SETSEL, 0, MAKELONG(x, y)); break; }
        case 'S': { int x = num(&a), y = num(&a); ret = SendMessage(ed, EM_SETSEL, 1, MAKELONG(x, y)); break; }
        case 't':
            txt(tt, a, sizeof tt);
            for (n = 0; tt[n]; n++)
                if (tt[n] != '\n')
                    SendMessage(ed, WM_CHAR, (BYTE)tt[n], 1);
            break;
        case 'b': ret = SendMessage(ed, WM_CHAR, 8, 1); break;
        case 'X': ret = SendMessage(ed, WM_CHAR, num(&a), 1); break;
        case 'k': realkey(*a, mods); pump(60); break;
        case 'u': ret = SendMessage(ed, EM_UNDO, 0, 0); break;
        case 'w': ret = SendMessage(ed, WM_UNDO, 0, 0); break;
        case 'z': ret = SendMessage(ed, EM_CANUNDO, 0, 0); break;
        case 'E': ret = SendMessage(ed, EM_EMPTYUNDOBUFFER, 0, 0); break;
        case 'p': ret = SendMessage(ed, WM_PASTE, 0, 0); break;
        case 'x': ret = SendMessage(ed, WM_CUT, 0, 0); break;
        case 'c': ret = SendMessage(ed, WM_COPY, 0, 0); break;
        case 'K': ret = SendMessage(ed, WM_CLEAR, 0, 0); break;
        case 'C': get_clip(t, 60); add(nrec, sizeof nrec, " clip="); add(nrec, sizeof nrec, t); break;
        case 'R': txt(tt, a, sizeof tt); ret = SendMessage(ed, EM_REPLACESEL, 0, (LPARAM)(LPSTR)tt); break;
        case 'T': txt(tt, a, sizeof tt); ret = SendMessage(ed, WM_SETTEXT, 0, (LPARAM)(LPSTR)tt); break;
        case 'F': ret = SendMessage(ed, EM_FMTLINES, num(&a), 0); break;
        case 'H': {
            HLOCAL hl = LocalAlloc(LMEM_MOVEABLE, 20);
            lstrcpy(LocalLock(hl), "handle");
            LocalUnlock(hl);
            ret = SendMessage(ed, EM_SETHANDLE, (WPARAM)hl, 0);
            break;
        }
        case 'M': ret = SendMessage(ed, EM_SETMODIFY, 0, 0); break;
        case 'L': ret = SendMessage(ed, EM_LIMITTEXT, num(&a), 0); break;
        case '@': { int x = num(&a), y = num(&a); click(ed, x, y, 0); break; }
        case '#': { int x = num(&a), y = num(&a); click(ed, x, y, 1); break; }
        case 'v': { int c = num(&a), p = num(&a); ret = SendMessage(ed, WM_VSCROLL, c, MAKELONG(p, 0)); break; }
        case 'h': { int c = num(&a), p = num(&a); ret = SendMessage(ed, WM_HSCROLL, c, MAKELONG(p, 0)); break; }
        case 'y': { int x = num(&a), y = num(&a); ret = SendMessage(ed, EM_LINESCROLL, 0, MAKELONG(x, y)); break; }
        case 'g': ret = SendMessage(ed, WM_VSCROLL, EM_GETTHUMB, 0); break;
        case 'W': {
            RECT r;
            GetWindowRect(ed, &r);
            MoveWindow(ed, 10, 10, num(&a), r.bottom - r.top, TRUE);
            break;
        }
        case 'G': ret = SendMessage(ed, WM_SETFONT, (WPARAM)fnt[num(&a)], 1); break;
        case 'D': dump_lines(name, ed); break;
        case 'Q': ret = grab_win(ed); break;
        case 'B': UpdateWindow(ed); call_mouse_event(0x8001, 0xFFFF, 0xFFFF, 0); pump(50); snap(); break;
        case 'P': {
            RECT r;
            SendMessage(ed, EM_GETRECT, 0, (LPARAM)(LPRECT)&r);
            wsprintf(t, " rect=%d,%d,%d,%d", r.left, r.top, r.right, r.bottom);
            add(nrec, sizeof nrec, t);
            break;
        }
        case 'Y': ret = SendMessage(ed, EM_SETWORDBREAKPROC, 0, (LPARAM)wbthunk); break;
        case 'Z': ret = SendMessage(ed, EM_SETWORDBREAKPROC, 0, 0); break;
        case 'V': ret = SendMessage(ed, WM_USER + 26, 0, (LPARAM)wboldthunk); break;
        case 'J': {
            DWORD p = SendMessage(ed, EM_GETWORDBREAKPROC, 0, 0);
            ret = p == (DWORD)wbthunk ? 1 : p == 0 ? 0 : 2;
            break;
        }
        case 'm': wbmode = num(&a); break;
        case 'r': ret = SendMessage(ed, EM_SETREADONLY, num(&a), 0); break;
        case 'O': {
            /* the Rect of a control made with ES_CENTER etc. is the
               script's own business: the caret at each line's start
               and end */
            int lc = (int)SendMessage(ed, EM_GETLINECOUNT, 0, 0), i;
            line[0] = 0;
            for (i = 0; i < lc && i < 20; i++) {
                int st = (int)SendMessage(ed, EM_LINEINDEX, i, 0), ll = (int)SendMessage(ed, EM_LINELENGTH, st, 0);
                POINT c1, c2;
                SendMessage(ed, EM_SETSEL, 0, MAKELONG(st, st));
                GetCaretPos(&c1);
                SendMessage(ed, EM_SETSEL, 0, MAKELONG(st + ll, st + ll));
                GetCaretPos(&c2);
                wsprintf(t, " %d,%d-%d,%d", c1.x, c1.y, c2.x, c2.y);
                add(line, sizeof line, t);
            }
            obs_str(name, line);
            continue;
        }
        default: break;
        }
        pump(20);
        sel = SendMessage(ed, EM_GETSEL, 0, 0);
        GetCaretPos(&cp);
        n = GetWindowText(ed, buf, sizeof buf);
        if (n > 100) {
            wsprintf(tt, "len=%d", n);
        } else
            esc(tt, sizeof tt, buf, n);
        wsprintf(t, "%s ret=%ld sel=%d-%d car=%d,%d top=%d cu=%d mod=%d n=%s txt=\"",
                 (LPSTR)tok, ret, LOWORD(sel), HIWORD(sel), cp.x, cp.y,
                 (int)SendMessage(ed, EM_GETFIRSTVISIBLELINE, 0, 0),
                 (int)SendMessage(ed, EM_CANUNDO, 0, 0), (int)SendMessage(ed, EM_GETMODIFY, 0, 0),
                 (LPSTR)nrec);
        lstrcpy(line, t);
        add(line, sizeof line, tt);
        add(line, sizeof line, "\"");
        if (wrec[0]) {
            add(line, sizeof line, " wb=");
            add(line, sizeof line, wrec);
        }
        out("OBS ");
        out(name);
        out("=");
        out(line);
        out("\r\n");
        nrec[0] = 0;
        wrec[0] = 0;
    }
    DestroyWindow(ed);
    ed = 0;
    pump(50);
}

#define MLW (WS_BORDER | ES_MULTILINE)
#define MLN (WS_BORDER | ES_MULTILINE | ES_AUTOHSCROLL)
#define LONGT "The~quick~brown~fox~jumps~over~the~lazy~dog.~Pack~my~box~with~five~dozen~liquor~jugs.||Second~paragraph~here~with~words."

static const SCRIPT scripts[] = {
    /* the caret over wrapped lines (system font, 130 wide) */
    { "keys", MLW, 130, 200, 0, LONGT,
      "^kH D O kD kD kD kD kD kD kD kU kU kU kU kU kU kU kE kR kL kE kD kH kU kE kE "
      "kE kR kR kL kL ^kR ^kR ^kR ^kR ^kR ^kR ^kR ^kL ^kL ^kL ^kL ^kL ^kL "
      "s10,10 +kD +kE +kR ^+kR kN ^kE kH kE kP s27,27 kD kU s0,0 kD kD kE kU kU "
      "@120,5 @120,20 @2,40 @60,200 #30,5 #30,20 #5,40" },
    { "keysms", MLW, 130, 200, 1, LONGT,
      "^kH D O kD kD kD kD kD kU kE kD kH kE kR kL kL ^kR ^kR ^kR ^kR ^kL ^kL #20,3" },
    { "keysfix", MLW, 130, 200, 2, LONGT,
      "^kH D O kD kD kD kD kD kU kE kD kH kE kR kL ^kR ^kR ^kR ^kL" },
    /* typing at a wrap */
    { "type", MLW, 130, 200, 0, LONGT,
      "s0,0 D tXXXXX D b b b D s20,20 t~ D t~~~~~~~~~~~~~~ D kE O" },
    /* scrolling */
    { "scroll", MLW | WS_VSCROLL, 130, 52, 0, LONGT,
      "Q B D y2,0 y100,0 y-1,0 y0,3 v1 v3 v7 v5,50 v4,30 g v6 kN kN ^kE ^kH S40,40 s40,40 S80,80 s200,200 s0,0" },
    { "scrollh", MLW | WS_HSCROLL, 130, 80, 0, LONGT, "y0,3 h1 h3 h5,50 kE" },
    /* ES_AUTOHSCROLL: where the view moves as the caret passes the edge */
    { "autoh", MLN, 130, 60, 0, NULL,
      "tabcdefghijklmn tlmnopqrstuvwxyz t0123456789ABCDE kH kE kL kL kL kL kL kL kL kL kL kL "
      "kL kL kL kL kL kL kL kL kL kL kL kL kL kL kL kL kL kL kL kL kL kL kL kL ^kE kR" },
    { "autohw", MLN | WS_HSCROLL, 130, 60, 0, NULL, "tabcdefghijklmnopqrstuvwxyz g h1 Tabc g" },
    /* resizing and fonts */
    { "resize", MLW, 130, 200, 0, LONGT,
      "s30,30 D W200 D W100 D W60 D W20 D W4 D W300 D W130 G1 D G2 D G3 D" },
    { "resizescroll", MLW, 130, 52, 0, LONGT, "^kE W80 W200 W130" },
    /* alignment */
    { "center", MLW | ES_CENTER, 130, 200, 0, LONGT, "D O Q B ^kH kD kE kD" },
    { "right", MLW | ES_RIGHT, 130, 200, 0, LONGT, "D O Q ^kH kD kE kD" },
    { "centerms", MLW | ES_CENTER, 130, 200, 1, LONGT, "D O Q" },
    /* the limit */
    { "limit", MLW, 80, 200, 0, NULL, "L20 tabcdefghij~klmnopqrstuvwxyz D L0 tABC D" },
    { "limitpaste", MLW, 80, 200, 0, "0123456789", "L12 s10,10 p D u" },
    /* EM_FMTLINES */
    { "fmt", MLW, 80, 200, 0, LONGT,
      "F1 D s0,0 tZ D kE kR F0 D F1 T" LONGT " F0 F0 D" },
    { "fmtcr", MLW, 80, 200, 0, "abc``/def~ghi~jkl~mno~pqr~stu", "D F0 D" },
    /* undo */
    { "u1", MLW, 300, 60, 0, "hello~world", "z s11,11 tabc u u u u" },
    { "u2", MLW, 300, 60, 0, "hello~world", "s5,5 tab kR tcd u u" },
    { "u3", MLW, 300, 60, 0, "hello~world", "s6,11 tX u u" },
    { "u4", MLW, 300, 60, 0, "hello~world", "s0,0 kd kd kd u u" },
    { "u5", MLW, 300, 60, 0, "hello~world", "s5,5 b b b u u" },
    { "u6", MLW, 300, 60, 0, "hello~world", "s5,5 kB kB kB u u" },
    { "u7", MLW, 300, 60, 0, "hello~world", "s5,5 tab kB kB kB kB u u" },
    { "u8", MLW, 300, 60, 0, "hello~world", "s0,5 p u u" },
    { "u9", MLW, 300, 60, 0, "hello~world", "s0,5 x C u u" },
    { "u10", MLW, 300, 60, 0, "hello~world", "s0,5 K u u" },
    { "u11", MLW, 300, 60, 0, "hello~world", "s0,5 c C u s3,3 tab c u" },
    { "u12", MLW, 300, 60, 0, "hello~world", "s3,3 Rxyz u u Ruvw Rrst u" },
    { "u13", MLW, 300, 60, 0, "hello~world", "s3,3 tab s0,0 u" },
    { "u14", MLW, 300, 60, 0, "hello~world", "s3,3 tab Tnew u" },
    { "u15", MLW, 300, 60, 0, "hello~world", "s3,3 tab E u" },
    { "u16", MLW, 300, 60, 0, "hello~world", "s3,3 tab M u" },
    { "u17", MLW, 300, 60, 0, "hello~world", "s3,3 tab w w" },
    { "u18", MLW, 300, 60, 0, "hello~world", "s3,3 tab X26 X26" },
    { "u19", MLW, 300, 60, 0, "hello~world", "s3,3 tab !kB !kB" },
    { "u20", MLW, 300, 60, 0, "hello~world", "s3,3 tab H u" },
    { "u21", MLW, 300, 60, 0, "hello~world", "s11,11 t~~ tab u" },
    { "u22", MLW, 300, 60, 0, "hello~world", "s11,11 tab| tcd u u" },
    { "u23", MLW, 300, 60, 0, "hello~world", "L14 s11,11 tabcdefgh u u" },
    { "u24", MLW, 300, 60, 0, "hello~world", "s11,11 tab u tcd u u" },
    { "u25", MLW, 300, 60, 0, "hello~world", "s2,2 kd tX u u" },
    { "u26", MLW, 300, 60, 0, "hello~world", "s11,11 tab s2,2 tcd u u" },
    { "u27", MLW, 300, 60, 0, "hello~world", "s0,11 b u u" },
    { "u28", MLW, 300, 60, 0, "hello~world", "s5,5 tab kL kL tcd u u" },
    { "u29", MLW, 300, 60, 0, "hello~world", "s5,5 p p u u" },
    { "u30", MLW, 300, 60, 0, "hello~world", "s5,5 tab s5,7 tX u u" },
    { "u31", MLW, 300, 60, 0, "hello~world", "s5,5 b tX u u" },
    { "u32", MLW, 300, 60, 0, "hello~world", "s5,5 tX b b u u" },
    { "u33", MLW, 300, 60, 0, "hello~world", "s5,5 tab u kR tc u" },
    { "u34", MLW, 300, 60, 0, "hello~world", "s0,0 kd kd tab u u" },
    { "u35", MLW, 300, 60, 0, "hello~world", "s5,5 tab F1 u F0 u" },
    { "u36", MLW, 300, 60, 0, "hello~world", "s5,5 tab +kE tX u u" },
    { "u37", MLW, 300, 60, 0, "hello~world", "s5,5 tab ^kZ ^kZ" },
    { "u38", MLW, 300, 60, 0, "hello~world", "s5,5 tab x u u u" },
    { "u39", MLW, 300, 60, 0, "hello~world", "s5,5 b b s3,3 b u u" },
    { "u40", MLW, 300, 60, 0, "hello~world", "s5,5 kd kd s3,3 kd u u" },
    { "u41", MLW, 300, 60, 0, "hello~world", "s5,5 tab s2,2 b u" },
    { "u42", MLW | ES_READONLY, 300, 60, 0, "hello~world", "s5,5 tab p u z" },
    { "u43", MLW, 300, 60, 0, "hello~world", "s0,5 tX s0,1 tY u u" },
    /* read-only: made so, and set by EM_SETREADONLY; typed, real keys */
    { "ro1", MLW | ES_READONLY, 300, 60, 0, "hello~world", "s5,5 kA tb kd kB p r0 tc" },
    { "ro2", MLW, 300, 60, 0, "hello~world", "r1 s5,5 kA tb kd kB p r0 tc" },
    { "ro3", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 300, 24, 0, "hello~world", "s5,5 kA tb kd p" },
    /* single-line controls */
    { "u1s", WS_BORDER | ES_AUTOHSCROLL, 300, 24, 0, "hello~world", "z s11,11 tabc u u u" },
    { "u3s", WS_BORDER | ES_AUTOHSCROLL, 300, 24, 0, "hello~world", "s6,11 tX u u" },
    { "u8s", WS_BORDER | ES_AUTOHSCROLL, 300, 24, 0, "hello~world", "s0,5 p u u" },
    { "u5s", WS_BORDER | ES_AUTOHSCROLL, 300, 24, 0, "hello~world", "s5,5 b b b u u" },
    { "u19s", WS_BORDER | ES_AUTOHSCROLL, 300, 24, 0, "hello~world", "s3,3 tab !kB !kB" },
    /* the clipboard keys */
    { "clip", MLW, 300, 60, 0, "hello~world",
      "s0,5 X3 C s0,5 X24 C u s0,0 X22 u s6,11 ^kC C s0,5 ^kX C u s0,0 ^kV u "
      "s6,11 ^kI C s0,5 +kd C u s0,0 +kI u" },
    { 0 }
};

/* the word-break procedure */
static const SCRIPT wbscripts[] = {
    { "wb", MLW, 130, 200, 0, NULL,
      "J Y J T" LONGT " D ^kH ^kR ^kR ^kR ^kL ^kL #30,5 s5,5 tQ W100 kE kD Z J T" LONGT " D" },
    { "wbhyph", MLW, 130, 200, 0, NULL,
      "m1 Y Tsuper-cali-fragi-listic-expi-ali-docious~ab-cd~e-f-g-h-i-j-k-l-m-n D ^kH ^kR ^kR ^kL m0" },
    { "wbnowrap", MLN, 130, 200, 0, NULL, "Y Tone~two~three~four~five~six~seven~eight D ^kH ^kR ^kR #20,5" },
    { "wbold", MLW, 130, 200, 0, NULL, "V J T" LONGT " D ^kH ^kR" },
    { "wbnone", MLW, 130, 200, 0, NULL, "m2 Y T" LONGT " D ^kH ^kR ^kR m0" },
    { 0 }
};

/* ---- word wrap: texts, widths, fonts ---- */
static const char *wtexts[] = {
    "The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs.",
    "Supercalifragilisticexpialidocious antidisestablishmentarianism ok",
    "Two  spaces   three    four\ttab\there and\r\nhard break\r\n\r\nafter blank line",
    "word                                        next   end      ",
    "lone\nLF and lone\rCR and\n\rLFCR and \r\r\n soft",
    "x-y-z hyphen-ated, commas,no-space;semi:colon/slash end",
    "\r\n\r\nab\r\n",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "\tTabbed\tline\twith\ttabs\tto\twrap\tat\tthe\tedge",
    0
};

static void sec_wrap(void)
{
    static const int widths[] = { 80, 130, 200 };
    int f, w, t;
    char nm[48];
    for (f = 0; f < 3; f++)
        for (w = 0; w < 3; w++)
            for (t = 0; wtexts[t]; t++) {
                HWND e = mk(MLW, widths[w], 150, f);
                SetWindowText(e, wtexts[t]);
                wsprintf(nm, "w.%d.%d.%d", f, widths[w], t);
                dump_lines(nm, e);
                if (widths[w] == 130) {
                    int r;
                    wsprintf(nm, "w.%d.%d.%d.sum", f, widths[w], t);
                    obs(nm, grab_win(e));
                    r = (int)SendMessage(e, EM_FMTLINES, 1, 0);
                    wsprintf(nm, "w.%d.%d.%d.fmt1", f, widths[w], t);
                    obs(nm, r);
                    log_text(nm, e);
                    wsprintf(nm, "w.%d.%d.%d.fmt1lc", f, widths[w], t);
                    obs(nm, (WORD)SendMessage(e, EM_GETLINECOUNT, 0, 0));
                    r = (int)SendMessage(e, EM_FMTLINES, 0, 0);
                    wsprintf(nm, "w.%d.%d.%d.fmt0", f, widths[w], t);
                    obs(nm, r);
                    log_text(nm, e);
                }
                DestroyWindow(e);
            }
    /* no wrap: the same with ES_AUTOHSCROLL */
    for (t = 0; wtexts[t]; t++) {
        HWND e = mk(MLN, 130, 150, 0);
        SetWindowText(e, wtexts[t]);
        wsprintf(nm, "nw.%d", t);
        dump_lines(nm, e);
        DestroyWindow(e);
    }
}

static void sec_edge(void)
{
    static const char *et[] = {
        "abcdefghij klmnopqrst uvwxyz",
        "abcdefghij  klmnopqrst",
        "abcdefghijklmnopqrstuvwxyz",
        "abc\tdefghij klm",
        "abcdefghij ",
        "abcdefghij \r\nxyz",
        0
    };
    int w, t;
    char nm[32], tt[16];
    for (t = 0; et[t]; t++) {
        line[0] = 0;
        for (w = 80; w <= 100; w++) {
            HWND e = mk(MLW, w, 60, 2);
            int lc, i;
            SetWindowText(e, et[t]);
            lc = (int)SendMessage(e, EM_GETLINECOUNT, 0, 0);
            wsprintf(tt, " %d:", w);
            add(line, sizeof line, tt);
            for (i = 1; i < lc; i++) {
                wsprintf(tt, "%d,", (int)SendMessage(e, EM_LINEINDEX, i, 0));
                add(line, sizeof line, tt);
            }
            DestroyWindow(e);
        }
        wsprintf(nm, "edge.%d", t);
        obs_str(nm, line);
    }
    /* MS Sans Serif, a pixel at a time */
    line[0] = 0;
    for (w = 60; w <= 90; w++) {
        HWND e = mk(MLW, w, 60, 1);
        int lc, i;
        SetWindowText(e, "The quick brown fox jumps");
        lc = (int)SendMessage(e, EM_GETLINECOUNT, 0, 0);
        wsprintf(tt, " %d:", w);
        add(line, sizeof line, tt);
        for (i = 1; i < lc; i++) {
            wsprintf(tt, "%d,", (int)SendMessage(e, EM_LINEINDEX, i, 0));
            add(line, sizeof line, tt);
        }
        DestroyWindow(e);
    }
    obs_str("edge.ms", line);
}

static void log_rect(const char *nm, HWND e)
{
    RECT r, c;
    char t[64];
    SendMessage(e, EM_GETRECT, 0, (LPARAM)(LPRECT)&r);
    GetClientRect(e, &c);
    wsprintf(t, "%d,%d,%d,%d client %d,%d", r.left, r.top, r.right, r.bottom, c.right, c.bottom);
    obs_str(nm, t);
}

static void sec_rect(void)
{
    static const DWORD st[] = {
        MLW, ES_MULTILINE, MLW | WS_VSCROLL, MLW | WS_VSCROLL | WS_HSCROLL, MLN, MLW | ES_CENTER,
        WS_BORDER | ES_AUTOHSCROLL, ES_AUTOHSCROLL, 0
    };
    int i, f;
    char nm[32];
    RECT r;
    for (i = 0; st[i]; i++)
        for (f = 0; f < 3; f++) {
            HWND e = mk(st[i], 130, 60, f);
            wsprintf(nm, "rect.%d.%d", i, f);
            log_rect(nm, e);
            DestroyWindow(e);
        }
    /* EM_SETRECT, EM_SETRECTNP and a resize after */
    {
        HWND e = mk(MLW, 130, 100, 0);
        SetWindowText(e, wtexts[0]);
        SetRect(&r, 5, 5, 85, 80);
        obs("setrect.ret", (WORD)SendMessage(e, EM_SETRECT, 0, (LPARAM)(LPRECT)&r));
        log_rect("setrect.1", e);
        dump_lines("setrect.1", e);
        obs("setrect.1.sum", grab_win(e));
        SetRect(&r, 0, 0, 60, 100);
        SendMessage(e, EM_SETRECTNP, 0, (LPARAM)(LPRECT)&r);
        log_rect("setrect.2", e);
        dump_lines("setrect.2", e);
        SetRect(&r, 0, 0, 400, 100);
        SendMessage(e, EM_SETRECT, 0, (LPARAM)(LPRECT)&r);
        log_rect("setrect.3", e);
        dump_lines("setrect.3", e);
        SetRect(&r, 10, 3, 90, 30);
        SendMessage(e, EM_SETRECT, 0, (LPARAM)(LPRECT)&r);
        MoveWindow(e, 10, 10, 200, 100, TRUE);
        log_rect("setrect.4", e);
        dump_lines("setrect.4", e);
        SendMessage(e, WM_SETFONT, (WPARAM)fnt[1], 1);
        log_rect("setrect.5", e);
        DestroyWindow(e);
        e = mk(WS_BORDER | ES_AUTOHSCROLL, 130, 24, 0);
        SetRect(&r, 5, 5, 85, 20);
        SendMessage(e, EM_SETRECT, 0, (LPARAM)(LPRECT)&r);
        log_rect("setrect.single", e);
        DestroyWindow(e);
    }
}

/* a large text: the time it takes (not compared) and the line count */
static void sec_big(void)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 20001);
    LPSTR p = GlobalLock(g);
    int i, k = 0;
    DWORD t0;
    HWND e;
    static const char w[] = "lorem ipsum dolor sit amet consectetur adipiscing elit ";
    for (i = 0; k < 20000 - 60; i++) {
        lstrcpy(p + k, w);
        k += sizeof w - 1;
        if (i % 7 == 6) {
            p[k++] = '\r';
            p[k++] = '\n';
        }
    }
    p[k] = 0;
    e = mk(MLW | WS_VSCROLL, 400, 200, 2);
    SendMessage(e, EM_LIMITTEXT, 0, 0);
    t0 = GetTickCount();
    SetWindowText(e, p);
    obs_env("big.settext.ms", (WORD)(GetTickCount() - t0));
    obs("big.lc", (WORD)SendMessage(e, EM_GETLINECOUNT, 0, 0));
    obs("big.li100", (WORD)SendMessage(e, EM_LINEINDEX, 100, 0));
    SetFocus(e);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(10000, 10000));
    t0 = GetTickCount();
    for (i = 0; i < 20; i++)
        SendMessage(e, WM_CHAR, 'x', 1);
    pump(20);
    obs_env("big.type.ms", (WORD)(GetTickCount() - t0));
    obs("big.lc2", (WORD)SendMessage(e, EM_GETLINECOUNT, 0, 0));
    obs("big.top", (WORD)SendMessage(e, EM_GETFIRSTVISIBLELINE, 0, 0));
    DestroyWindow(e);
    GlobalUnlock(g);
    GlobalFree(g);
}

LRESULT FAR PASCAL __export __loadds main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_COMMAND && LOWORD(l) && (HWND)LOWORD(l) == ed) {
        char t[8];
        WORD c = HIWORD(l);
        t[0] = c == EN_CHANGE ? 'C' : c == EN_UPDATE ? 'U' : c == EN_SETFOCUS ? 'S' :
               c == EN_KILLFOCUS ? 'K' : c == EN_MAXTEXT ? 'M' : c == EN_ERRSPACE ? 'E' :
               c == EN_VSCROLL ? 'V' : c == EN_HSCROLL ? 'H' : '?';
        t[1] = 0;
        add(nrec, sizeof nrec, t);
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HDC dc;
    int i;
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US35 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)main_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US35M";
    RegisterClass(&wc);
    hMain = CreateWindow("US35M", "US35", WS_OVERLAPPEDWINDOW, 0, 0, 500, 400, 0, 0, hi, NULL);
    ShowWindow(hMain, SW_SHOWNORMAL);
    UpdateWindow(hMain);
    pump(300);
    call_mouse_event(0x8001, 0xFFFF, 0xFFFF, 0);
    dc = GetDC(NULL);
    fnt[1] = CreateFont(-MulDiv(8, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0, FW_BOLD, 0, 0, 0,
                        ANSI_CHARSET, 0, 0, 0, VARIABLE_PITCH | FF_SWISS, "MS Sans Serif");
    fnt[2] = GetStockObject(SYSTEM_FIXED_FONT);
    fnt[3] = GetStockObject(SYSTEM_FONT);
    wbthunk = MakeProcInstance((FARPROC)wbproc, hi);
    wboldthunk = MakeProcInstance((FARPROC)wbold, hi);   /* WM_SETFONT 0 hung 3.1 */
    ReleaseDC(NULL, dc);
    out("wrap\r\n");
    sec_wrap();
    out("edge\r\n");
    sec_edge();
    out("rect\r\n");
    sec_rect();
    for (i = 0; scripts[i].name; i++) {
        out("script ");
        out(scripts[i].name);
        out("\r\n");
        run_script(&scripts[i]);
    }
    out("big\r\n");
    sec_big();
    for (i = 0; wbscripts[i].name; i++) {
        out("script ");
        out(wbscripts[i].name);
        out("\r\n");
        run_script(&wbscripts[i]);
    }
    out("US35 end\r\n");
    DeleteObject(fnt[1]);
    DestroyWindow(hMain);
    return 0;
}
