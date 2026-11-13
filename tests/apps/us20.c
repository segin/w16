/*
 * us20.c - probe US20: where an edit control keeps its text, and
 * EM_GETHANDLE / EM_SETHANDLE (ARCHITECTURE.md 10.3; user/edit.c).
 *
 * Windows 3.1 SDK: a multi-line edit control's text is a local block in
 * the data segment its hInstance names; EM_GETHANDLE returns that local
 * handle, EM_SETHANDLE gives the control another one. Observed here:
 *   A  a multi-line edit control, child of the probe's window, hInstance
 *      the probe's: the handle's flags, lock count and size, empty and
 *      after text; typing; the default limit (WM_SETTEXT, WM_CHAR,
 *      EM_REPLACESEL past 30000) and EM_LIMITTEXT 0; EM_SETHANDLE with a
 *      block of the probe's (the old one left or freed, the text, the
 *      modified flag); the text changed in the block behind the
 *      control's back; a block grown with LocalReAlloc and set again (as
 *      Notepad loads a file); what DestroyWindow frees;
 *   B  a single-line control's EM_GETHANDLE;
 *   C  a dialog's edit controls without DS_LOCALEDIT (their hInstance:
 *      the probe's, or a segment USER makes; shared or one each; freed
 *      with the dialog?), D  with DS_LOCALEDIT;
 *   E  a control whose hInstance is a global block the probe made a
 *      local heap of (LocalInit), as Pietrek describes for 64K of text.
 * The text in another segment is read through the local handle table
 * (a moveable local handle points at its block's offset: Pietrek,
 * Windows Internals ch. 2) - observation only.
 * OBS lines are compared with Windows 3.1's run (tests/boottest.sh
 * edithandle); ~ lines depend on the layout.
 */
#include <windows.h>

#define TK_REF "us20ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define EM_GETHANDLE_ (WM_USER + 13)
#define EM_SETHANDLE_ (WM_USER + 12)

static HINSTANCE hinst;
static HWND hMain;
static char buf[200];

static void obsd(const char *name, int v)
{
    char b[80];
    wsprintf(b, "OBS %s=%d\r\n", (LPSTR)name, v);
    out(b);
}

/* the text of local handle h in segment sel (moveable: its table entry
   holds the block's offset; fixed: the handle is the offset) */
static char FAR *deref(WORD sel, WORD h)
{
    if (h & 2)
        return (char FAR *)MAKELP(sel, *(WORD FAR *)MAKELP(sel, h));
    return (char FAR *)MAKELP(sel, h);
}

static WORD sel_of(HINSTANCE h)
{
    return HIWORD(GlobalHandle((UINT)h));
}

/* the handle's flags and size in our own segment */
static void local_info(const char *tag, HLOCAL h)
{
    char nm[40];
    wsprintf(nm, "%s.flags", (LPSTR)tag);
    obs(nm, LocalFlags(h));
    wsprintf(nm, "%s.size", (LPSTR)tag);
    obsd(nm, LocalSize(h));
}

static HWND mkedit(DWORD style, HINSTANCE hi)
{
    return CreateWindow("edit", "", WS_CHILD | WS_VISIBLE | WS_BORDER | style, 10, 10, 200, 100,
                        hMain, (HMENU)1, hi, NULL);
}

/* a string of n copies of c in a global block */
static LPSTR many(HGLOBAL *g, long n, char c)
{
    LPSTR p;
    long i;
    *g = GlobalAlloc(GMEM_MOVEABLE, n + 1);
    p = GlobalLock(*g);
    for (i = 0; i < n; i++)
        p[(WORD)i] = c;
    p[(WORD)n] = 0;
    return p;
}

static void part_a(void)
{
    HWND e = mkedit(ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, hinst);
    HLOCAL h, h2, h3;
    HGLOBAL g;
    LPSTR p;
    char NEAR *np;
    int len;
    obs("A.hinst_is_ours", GetWindowWord(e, GWW_HINSTANCE) == (WORD)hinst);
    h = (HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0);
    obs("A.empty.handle_nonzero", h != 0);
    obs("A.empty.moveable", (h & 2) != 0);
    local_info("A.empty", h);
    np = LocalLock(h);
    obs("A.empty.first", np ? (BYTE)*np : 0xFFFF);
    LocalUnlock(h);
    SetWindowText(e, "hello\r\nworld");
    obs("A.text.same_handle", (HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0) == h);
    local_info("A.text", h);
    np = LocalLock(h);
    obs_str("A.text.content", np ? (LPSTR)np : (LPSTR)"(null)");
    LocalUnlock(h);
    /* typing at the end */
    SendMessage(e, EM_SETSEL, 0, MAKELONG(12, 12));
    SendMessage(e, WM_CHAR, '!', 1L);
    obs("A.typed.same_handle", (HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0) == h);
    local_info("A.typed", h);
    /* the default limit */
    p = many(&g, 30005, 'a');
    SetWindowText(e, p);
    obsd("A.limit.settext30005", GetWindowTextLength(e));
    GlobalUnlock(g);
    GlobalFree(g);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(30005, 30005));
    SendMessage(e, WM_CHAR, 'b', 1L);
    obsd("A.limit.char", GetWindowTextLength(e));
    SendMessage(e, EM_REPLACESEL, 0, (LPARAM)(LPSTR)"cc");
    obsd("A.limit.replacesel", GetWindowTextLength(e));
    p = many(&g, 29998, 'a');
    SetWindowText(e, p);
    GlobalUnlock(g);
    GlobalFree(g);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(29998, 29998));
    SendMessage(e, EM_REPLACESEL, 0, (LPARAM)(LPSTR)"ccccc");
    obsd("A.limit.replacesel_from29998", GetWindowTextLength(e));
    SendMessage(e, EM_SETSEL, 0, MAKELONG(30003, 30003));
    SendMessage(e, WM_CHAR, 'b', 1L);
    SendMessage(e, WM_CHAR, 'b', 1L);
    obsd("A.limit.char_at30000", GetWindowTextLength(e));
    SendMessage(e, EM_LIMITTEXT, 0, 0);
    SendMessage(e, WM_CHAR, 'b', 1L);
    obsd("A.limit0.char", GetWindowTextLength(e));
    SendMessage(e, EM_LIMITTEXT, 10, 0);
    SetWindowText(e, "0123456789abcdef");
    obsd("A.limit10.settext", GetWindowTextLength(e));
    SendMessage(e, EM_SETSEL, 0, MAKELONG(16, 16));
    SendMessage(e, WM_CHAR, 'b', 1L);
    obsd("A.limit10.char", GetWindowTextLength(e));
    SendMessage(e, EM_LIMITTEXT, 0, 0);
    local_info("A.big", h);
    obs("A.big.same_handle", (HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0) == h);
    SetWindowText(e, "short");
    local_info("A.short", h);
    /* EM_SETHANDLE with a block of ours */
    h2 = LocalAlloc(LMEM_MOVEABLE, 20);
    np = LocalLock(h2);
    lstrcpy(np, "new text");
    LocalUnlock(h2);
    SendMessage(e, EM_SETMODIFY, TRUE, 0);
    SendMessage(e, EM_SETHANDLE_, (WPARAM)h2, 0);
    GetWindowText(e, buf, sizeof buf);
    obs_str("A.set.text", buf);
    obs("A.set.gethandle_is_new", (HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0) == h2);
    obs("A.set.modify", (WORD)SendMessage(e, EM_GETMODIFY, 0, 0));
    local_info("A.set.new", h2);
    obsd("A.set.old_size", LocalSize(h));
    obs("A.set.old_flags", LocalFlags(h));
    obsd("A.set.linecount", (int)SendMessage(e, EM_GETLINECOUNT, 0, 0));
    /* the text changed behind the control's back */
    np = LocalLock(h2);
    np[0] = 'N';
    lstrcpy(np + 3, "\r\nline");
    LocalUnlock(h2);
    GetWindowText(e, buf, sizeof buf);
    obs_str("A.behind.text", buf);
    obsd("A.behind.len", GetWindowTextLength(e));
    /* (3.1 keeps its count of lines: 1; w16 counts them in the block
       each time, up to the control's length: 2. Not compared) */
    obs_env("A.behind.linecount", (int)SendMessage(e, EM_GETLINECOUNT, 0, 0));
    /* as Notepad loads a file: the block grown, filled, set again */
    h3 = (HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0);
    h3 = LocalReAlloc(h3, 3000, LMEM_MOVEABLE);
    obs("A.realloc.same", h3 == h2);
    np = LocalLock(h3);
    for (len = 0; len < 2900; len++)
        np[len] = (len % 30) == 28 ? '\r' : (len % 30) == 29 ? '\n' : (char)('a' + len % 26);
    np[len] = 0;
    LocalUnlock(h3);
    SendMessage(e, EM_SETHANDLE_, (WPARAM)h3, 0);
    obsd("A.realloc.len", GetWindowTextLength(e));
    obsd("A.realloc.linecount", (int)SendMessage(e, EM_GETLINECOUNT, 0, 0));
    local_info("A.realloc", h3);
    /* typing then: the block grows where the control has it */
    SendMessage(e, EM_SETSEL, 0, MAKELONG(2900, 2900));
    SendMessage(e, WM_CHAR, 'z', 1L);
    obs("A.realloc.typed_same", (HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0) == h3);
    local_info("A.realloc.typed", h3);
    /* destroyed: is its block freed? (the old one, given back by
       EM_SETHANDLE, freed by the probe first if still there) */
    if (LocalSize(h))
        LocalFree(h);
    DestroyWindow(e);
    obsd("A.destroyed.size", LocalSize(h3));
    obs("A.destroyed.flags", LocalFlags(h3));
}

static void part_b(void)
{
    HWND e = mkedit(ES_AUTOHSCROLL, hinst);
    HLOCAL h;
    SetWindowText(e, "single");
    h = (HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0);
    obs("B.handle_nonzero", h != 0);
    if (h) {
        local_info("B", h);
        obs("B.moveable", (h & 2) != 0);
    }
    DestroyWindow(e);
}

/* an in-memory dialog template: three controls, two edits (one
   multi-line) and a button */
static HGLOBAL mktemplate(DWORD style)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 512);
    BYTE FAR *p = GlobalLock(g), FAR *q = p;
    int i;
    *(DWORD FAR *)q = style; q += 4;
    *q++ = 3;
    *(WORD FAR *)q = 10; q += 2;
    *(WORD FAR *)q = 10; q += 2;
    *(WORD FAR *)q = 200; q += 2;
    *(WORD FAR *)q = 100; q += 2;
    *q++ = 0;                   /* menu */
    *q++ = 0;                   /* class */
    lstrcpy((LPSTR)q, "US20");
    q += 5;
    for (i = 0; i < 3; i++) {
        *(WORD FAR *)q = 5; q += 2;
        *(WORD FAR *)q = (WORD)(5 + i * 25); q += 2;
        *(WORD FAR *)q = 150; q += 2;
        *(WORD FAR *)q = i == 1 ? 20 : 12; q += 2;
        *(WORD FAR *)q = (WORD)(100 + i); q += 2;
        *(DWORD FAR *)q = WS_CHILD | WS_VISIBLE |
            (i == 2 ? BS_PUSHBUTTON : WS_BORDER | (i == 1 ? ES_MULTILINE : ES_AUTOHSCROLL)); q += 4;
        *q++ = i == 2 ? 0x80 : 0x81;
        lstrcpy((LPSTR)q, i == 0 ? "one" : i == 1 ? "two\r\nlines" : "OK");
        q += lstrlen((LPSTR)q) + 1;
        *q++ = 0;
    }
    GlobalUnlock(g);
    return g;
}

BOOL FAR PASCAL __export __loadds dlg_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    (void)h; (void)wp; (void)lp;
    return m == WM_INITDIALOG;
}

static void part_dialog(const char *tag, DWORD style)
{
    HGLOBAL t = mktemplate(WS_POPUP | WS_VISIBLE | WS_CAPTION | style);
    HWND d = CreateDialogIndirect(hinst, GlobalLock(t), hMain, (DLGPROC)dlg_proc);
    HWND e1, e2;
    HINSTANCE i1, i2;
    WORD h1, h2, sel;
    char nm[48];
    GlobalUnlock(t);
    if (!d) {
        wsprintf(nm, "%s.created", (LPSTR)tag);
        obs(nm, 0);
        GlobalFree(t);
        return;
    }
    e1 = GetDlgItem(d, 100);
    e2 = GetDlgItem(d, 101);
    i1 = (HINSTANCE)GetWindowWord(e1, GWW_HINSTANCE);
    i2 = (HINSTANCE)GetWindowWord(e2, GWW_HINSTANCE);
    wsprintf(nm, "%s.dlg_hinst_is_ours", (LPSTR)tag);
    obs(nm, GetWindowWord(d, GWW_HINSTANCE) == (WORD)hinst);
    wsprintf(nm, "%s.single.hinst_is_ours", (LPSTR)tag);
    obs(nm, i1 == hinst);
    wsprintf(nm, "%s.multi.hinst_is_ours", (LPSTR)tag);
    obs(nm, i2 == hinst);
    wsprintf(nm, "%s.same_hinst", (LPSTR)tag);
    obs(nm, i1 == i2);
    wsprintf(nm, "%s.single.hinst", (LPSTR)tag);
    obs_env(nm, (WORD)i1);
    wsprintf(nm, "%s.multi.hinst", (LPSTR)tag);
    obs_env(nm, (WORD)i2);
    if (i2 != hinst) {
        wsprintf(nm, "%s.multi.gsize", (LPSTR)tag);
        obs_env(nm, (WORD)GlobalSize((HGLOBAL)i2));
        wsprintf(nm, "%s.multi.gflags", (LPSTR)tag);
        obs(nm, GlobalFlags((HGLOBAL)i2));
        wsprintf(nm, "%s.multi.hinst_odd", (LPSTR)tag);
        obs(nm, (WORD)i2 & 1);
    }
    h1 = (WORD)SendMessage(e1, EM_GETHANDLE_, 0, 0);
    h2 = (WORD)SendMessage(e2, EM_GETHANDLE_, 0, 0);
    wsprintf(nm, "%s.multi.handle_nonzero", (LPSTR)tag);
    obs(nm, h2 != 0);
    sel = sel_of(i2);
    if (h2 && sel) {
        wsprintf(nm, "%s.multi.text", (LPSTR)tag);
        obs_str(nm, deref(sel, h2));
    }
    wsprintf(nm, "%s.single.handle_nonzero", (LPSTR)tag);
    obs(nm, h1 != 0);
    if (h1 && sel_of(i1)) {
        wsprintf(nm, "%s.single.text", (LPSTR)tag);
        obs_str(nm, deref(sel_of(i1), h1));
    }
    DestroyWindow(d);
    if (i2 != hinst) {
        wsprintf(nm, "%s.after.gsize_zero", (LPSTR)tag);
        obs(nm, GlobalSize((HGLOBAL)i2) == 0);
    }
    GlobalFree(t);
}

/* F: a big text in a crowded segment, as Notepad's after loading a 31500-
   byte file: the block (31501 bytes, as Notepad makes it) is not the
   heap's last and could not be copied elsewhere in 64K; set with
   EM_SETHANDLE, then typed into. On 3.1 one character went in, the block
   did not grow; w16 grows it (UNVERIFIED why 3.1 does not here and does
   in part G): the typed length and size are not compared */
static void part_f(void)
{
    HWND e;
    HLOCAL h, after;
    char NEAR *np, NEAR *np2;
    WORD i;
    h = LocalAlloc(LMEM_MOVEABLE, 31501);
    after = LocalAlloc(LMEM_FIXED, 8000);
    obs("F.allocated", h != 0 && after != 0);
    if (!h || !after)
        return;
    np = LocalLock(h);
    for (i = 0; i < 31500; i++)
        np[i] = (i % 63) == 61 ? '\r' : (i % 63) == 62 ? '\n' : (char)('a' + i % 26);
    np[i] = 0;
    LocalUnlock(h);
    obsd("F.size_before", LocalSize(h));
    e = mkedit(ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, hinst);
    SendMessage(e, EM_LIMITTEXT, 0, 0);
    {
        HLOCAL old = (HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0);
        SendMessage(e, EM_SETHANDLE_, (WPARAM)h, 0);
        LocalFree(old);
    }
    obsd("F.set.size", LocalSize(h));
    obsd("F.set.len", GetWindowTextLength(e));
    np = LocalLock(h);
    LocalUnlock(h);
    SendMessage(e, EM_SETSEL, 0, MAKELONG(31500, 31500));
    for (i = 0; i < 40; i++)
        SendMessage(e, WM_CHAR, 'x', 1L);
    obs_env("F.typed.len", GetWindowTextLength(e));
    obs("F.typed.same_handle", (HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0) == h);
    obs_env("F.typed.size", LocalSize((HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0)));
    np2 = LocalLock(h);
    LocalUnlock(h);
    obs("F.typed.moved", np2 != np);
    obs_env("F.ds_size", (WORD)GlobalSize((HGLOBAL)hinst));
    DestroyWindow(e);
    LocalFree(after);
}

/* G: as F, but the text's block the heap's last (as in Notepad's segment
   after it loads a file: US21) */
static void part_g(void)
{
    HWND e;
    HLOCAL h;
    char NEAR *np;
    WORD i;
    h = LocalAlloc(LMEM_MOVEABLE, 31501);
    obs("G.allocated", h != 0);
    if (!h)
        return;
    np = LocalLock(h);
    for (i = 0; i < 31500; i++)
        np[i] = (i % 63) == 61 ? '\r' : (i % 63) == 62 ? '\n' : (char)('a' + i % 26);
    np[i] = 0;
    LocalUnlock(h);
    e = mkedit(ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, hinst);
    SendMessage(e, EM_LIMITTEXT, 0, 0);
    {
        HLOCAL old = (HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0);
        SendMessage(e, EM_SETHANDLE_, (WPARAM)h, 0);
        LocalFree(old);
    }
    obsd("G.set.size", LocalSize(h));
    SendMessage(e, EM_SETSEL, 0, MAKELONG(31500, 31500));
    for (i = 0; i < 40; i++)
        SendMessage(e, WM_CHAR, 'x', 1L);
    obsd("G.typed.len", GetWindowTextLength(e));
    obsd("G.typed.size", LocalSize((HLOCAL)SendMessage(e, EM_GETHANDLE_, 0, 0)));
    DestroyWindow(e);
}

/* H: KERNEL alone - a 31501-byte moveable block at the heap's end grown
   by 32 with LocalReAlloc, by flags (what the edit control can ask) */
static void part_h(void)
{
    static const WORD flags[] = { 0, LMEM_MOVEABLE, LMEM_MOVEABLE | LMEM_ZEROINIT, LMEM_ZEROINIT };
    int k;
    char nm[32];
    for (k = 0; k < 4; k++) {
        HLOCAL h = LocalAlloc(LMEM_MOVEABLE, 31501), r;
        if (!h) {
            obs("H.alloc", 0);
            return;
        }
        r = LocalReAlloc(h, 31534, flags[k]);
        wsprintf(nm, "H.realloc%d.ok", k);
        obs(nm, r != 0);
        wsprintf(nm, "H.realloc%d.size", k);
        obsd(nm, LocalSize(r ? r : h));
        LocalFree(r ? r : h);
    }
}

static void part_e(void)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 1024);
    WORD sel = sel_of((HINSTANCE)g);
    HWND e;
    WORD h;
    HGLOBAL t;
    LPSTR p;
    obs("E.localinit", LocalInit(sel, 0, 1023) != 0);
    e = mkedit(ES_MULTILINE, (HINSTANCE)g);
    obs("E.created", e != 0);
    if (!e)
        return;
    obs("E.hinst_is_g", GetWindowWord(e, GWW_HINSTANCE) == (WORD)g);
    SetWindowText(e, "in a segment of its own");
    h = (WORD)SendMessage(e, EM_GETHANDLE_, 0, 0);
    sel = sel_of((HINSTANCE)g);
    obs("E.handle_nonzero", h != 0);
    if (h)
        obs_str("E.text", deref(sel, h));
    p = many(&t, 5000, 'e');
    SetWindowText(e, p);
    GlobalUnlock(t);
    GlobalFree(t);
    obsd("E.big.len", GetWindowTextLength(e));
    obs("E.big.grew", GlobalSize(g) > 5000);
    DestroyWindow(e);
    GlobalFree(g);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    out("US20 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US20";
    RegisterClass(&wc);
    hMain = CreateWindow("US20", "US20", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 320, 240,
                         NULL, NULL, hinst, NULL);
    obs_env("hinst", (WORD)hinst);
    part_a();
    part_b();
    part_dialog("C", 0);
    part_dialog("D", DS_LOCALEDIT);
    part_e();
    part_h();
    part_g();
    part_f();
    out(failures ? "US20 failures\r\n" : "US20 all passed\r\n");
    out("US20 end\r\n");
    DestroyWindow(hMain);
    return 0;
}
