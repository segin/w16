/*
 * us27.c - probe US27: USER's functions given a bad pointer (ARCHITECTURE.md
 * 9.4 and 10, USER's "__GP" table).
 *
 * Calls USER's pointer-taking functions (strings, RECT and POINT, structures,
 * buffers) with each of these instead of a good pointer:
 *   N  NULL (0:0)
 *   F  a freed selector (a block's, GlobalFree), offset 0
 *   P  a not-present selector (a discardable block, discarded), offset 0
 *   L  an offset past the limit of a small block
 *   E  the edge of that block: 4 good bytes ("abcd") and then the limit
 *   R  for output buffers: the probe's code selector (not writable)
 * (F and P are made afresh for each case: a freed selector is soon used
 * again) and logs, as OBS lines (tklog.h), for each function and kind
 * ("Name.K"): whether the call came back (.ok: 0 when the fault it caused
 * would have ended the task, see below), its return value, what it left
 * behind (window text, the 4 edge bytes as .e0 .e1, ...).
 *
 * The probe hooks the faults itself (us27s.asm, DPMI exception handlers
 * in front of KERNEL's): every fault a call causes gives a "FLT" line
 * with the exception and the module and segment it happened in (named
 * with TOOLHELP where the install has it; not compared: it shows how 3.1
 * validates, not what a program sees). A fault KERNEL would not recover
 * through a "__GP" table throws back here, logged as .ok=0: Windows 3.1
 * would have ended the task with an Application Error there. A few faults
 * end the task on 3.1 out of the hooks' reach (KERNEL's own Application
 * Error); tests/us27run.sh boots again past each such case.
 *
 * A "RUN Name.K" line before each case. WIN.INI [US27]: see tmain. Cases
 * us27ref.h has with .ok 0 are skipped unless explore=1: w16 need not end
 * a task where 3.1 does.
 *
 * Run it as the shell in DOSBox-X (dropin.sh -p 386x; tools/us27ref.py:
 * what its segment limit checks cover): tests/us27run.sh for Windows 3.1
 * (dropin.sh -r, KRNL286; -r -3, KRNL386), boot test case gpuser for w16.
 * us27ref.h holds 3.1's values (tools/us27ref.py, tools/obsref.py).
 */
#include <windows.h>
#include <toolhelp.h>

#define TK_REF "us27ref.h"
#include "tklog.h"

extern WORD __cdecl flt_n, flt_int, flt_cs, flt_ip, flt_rec, armed;
extern WORD __cdecl np_sel;
extern void __cdecl __far flt_install(void);
extern void __cdecl __far flt_remove(void);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HINSTANCE hinst;
static HWND hwnd, hdlg, hedit, hlist, hcombo;
static HDC hdc;
static HMENU hmenu;
static HACCEL hacc;
static HBRUSH hbr;
static UINT fmt;
static RECT r, r1, r2;
static POINT pt;
static int ix, iy;
static WNDCLASS wc;
static char buf[300];
static BYTE bits[128];
static CATCHBUF cb;

/* the bad pointers */
enum { KN, KF, KP, KL, KE, KR, NKIND };
static LPSTR bad[NKIND];
static WORD esel, eoff;                 /* the edge block, its last 4 bytes */

static BOOL (WINAPI *p_GlobalEntryHandle)(LPGLOBALENTRY, HGLOBAL);
static HMODULE (WINAPI *p_ModuleFindHandle)(LPMODULEENTRY, HMODULE);

static WORD lar_ah(WORD sel);
#pragma aux lar_ah = \
    "xor ax, ax" \
    "lar ax, dx" \
    "jnz no" \
    "mov al, ah" \
    "xor ah, ah" \
    "jmp done" \
    "no: mov ax, 0FFFFh" \
    "done:" \
    parm [dx] value [ax];
static WORD lsl_(WORD sel);
#pragma aux lsl_ = \
    "xor ax, ax" \
    "lsl ax, dx" \
    parm [dx] value [ax];

void __cdecl __far us27_throw(void)
{
    Throw(cb, 1);
}

/* ---- naming ---- */
static char nm[48], nm2[56];
static void name_case(const char *n, int k)
{
    int i = 0;
    while (*n)
        nm[i++] = *n++;
    nm[i++] = '.';
    nm[i++] = "NFPLER"[k];
    nm[i] = 0;
}
static void side(const char *suffix, WORD v)
{
    int i = 0, j = 0;
    while (nm[j])
        nm2[i++] = nm[j++];
    nm2[i++] = '.';
    while (*suffix)
        nm2[i++] = *suffix++;
    nm2[i] = 0;
    obs(nm2, v);
}

static void reset(void)
{
    SetRect(&r, 10, 20, 110, 120);
    SetRect(&r1, 0, 0, 50, 50);
    SetRect(&r2, 25, 25, 75, 75);
    pt.x = 5, pt.y = 6;
    ix = iy = 0x7777;
    buf[0] = 'Z', buf[1] = 0;
}

/* the probe's WM_USER + 1 messages still queued, removed (only those: a
   WM_PAINT is never removed by PeekMessage) */
static int drain(void)
{
    int n = 0;
    MSG m;
    while (PeekMessage(&m, hwnd, WM_USER + 1, WM_USER + 1, PM_REMOVE))
        n++;
    return n;
}

/* ---- the cases: each calls one function with B() in the place tested ---- */

/* The case's bad pointer. F's freed selector is made here, right as the
   call is made: one freed earlier could have been handed out again
   (after a few such cases the run stopped, at a different place each
   time, the call having worked on some other block). */
static int ck;
static WORD flar;
static LPSTR B(void)
{
    if (ck == KF) {
        HGLOBAL h = GlobalAlloc(GMEM_FIXED, 64);
        WORD s = SELECTOROF(GlobalLock(h));
        GlobalUnlock(h);
        GlobalFree(h);
        bad[KF] = (LPSTR)MAKELP(s, 0);
        flar = lar_ah(s);
    }
    return bad[ck];
}
#define RCT(p) ((RECT FAR *)(p))
#define PTS(p) ((POINT FAR *)(p))

static DWORD c_SetWindowText(void)
{
    SetWindowText(hwnd, "init");
    SetWindowText(hwnd, B());
    side("len", GetWindowTextLength(hwnd));
    return 0;
}
static DWORD c_GetWindowText(void) { SetWindowText(hwnd, "init text"); return GetWindowText(hwnd, B(), 10); }
static DWORD c_GetClassName(void) { return GetClassName(hwnd, B(), 10); }
static DWORD c_DrawTextS(void) { DWORD v = DrawText(hdc, B(), -1, &r, 0); side("r", r.right); return v; }
static DWORD c_DrawTextN(void) { return DrawText(hdc, B(), 8, &r, 0); }
static DWORD c_DrawTextC(void) { DWORD v = DrawText(hdc, B(), 8, &r, DT_CALCRECT); side("r", r.right); return v; }
static DWORD c_DrawTextR(void) { return DrawText(hdc, "abc", 3, RCT(B()), 0); }
static DWORD c_lstrcmp1(void) { return lstrcmp(B(), "a"); }
static DWORD c_lstrcmp2(void) { return lstrcmp("a", B()); }
static DWORD c_lstrcmpi(void) { return lstrcmpi(B(), "a"); }
static DWORD c_AnsiUpper(void) { return (DWORD)AnsiUpper(B()); }
static DWORD c_AnsiLower(void) { return (DWORD)AnsiLower(B()); }
static DWORD c_AnsiUpperBuff(void) { return AnsiUpperBuff(B(), 8); }
static DWORD c_AnsiNext(void) { return (DWORD)AnsiNext(B()); }
static DWORD c_AnsiPrev(void) { return (DWORD)AnsiPrev(B(), B() + 2); }
static DWORD c_wsprintfF(void) { DWORD v = wsprintf(buf, B()); side("b0", (BYTE)buf[0]); return v; }
static DWORD c_wsprintfO(void) { return wsprintf(B(), "abcdefgh"); }
static DWORD c_wsprintfS(void) { DWORD v = wsprintf(buf, "x%s", B()); side("b1", (BYTE)buf[1]); return v; }
static DWORD c_wvsprintfA(void) { DWORD v = wvsprintf(buf, "%d %d %d", B()); side("b0", (BYTE)buf[0]); return v; }
static DWORD c_wvsprintfF(void) { int a[2]; a[0] = 1, a[1] = 2; return wvsprintf(buf, B(), a); }
static DWORD c_wvsprintfO(void) { int a[2]; a[0] = 1, a[1] = 2; return wvsprintf(B(), "%d%d", a); }
static DWORD c_LoadString(void) { return LoadString(hinst, 1, B(), 10); }
static DWORD c_GetClassInfoO(void) { return GetClassInfo(NULL, "BUTTON", (WNDCLASS FAR *)B()); }
static DWORD c_GetClassInfoN(void) { WNDCLASS w; return GetClassInfo(NULL, B(), &w); }
static DWORD c_FindWindowC(void) { return FindWindow(B(), NULL) != 0; }
static DWORD c_FindWindowT(void) { return FindWindow(NULL, B()) != 0; }
static DWORD c_RegisterClassP(void) { return RegisterClass((WNDCLASS FAR *)B()) != 0; }
static DWORD c_RegisterClassN(void)
{
    WNDCLASS w = wc;
    ATOM a;
    w.lpszClassName = B();
    a = RegisterClass(&w);
    return a != 0;
}
static DWORD c_RegisterClassM(void)
{
    WNDCLASS w = wc;
    w.lpszClassName = "US27M";
    w.lpszMenuName = B();
    if (!RegisterClass(&w))
        return 0;
    UnregisterClass("US27M", hinst);
    return 1;
}
static DWORD c_RegisterWindowMessage(void) { return RegisterWindowMessage(B()) != 0; }
static DWORD c_CreateWindowC(void)
{
    HWND h = CreateWindow(B(), "x", WS_CHILD, 0, 0, 10, 10, hwnd, (HMENU)99, hinst, NULL);
    if (h)
        DestroyWindow(h);
    return h != 0;
}
static DWORD c_CreateWindowT(void)
{
    HWND h = CreateWindow("BUTTON", B(), WS_CHILD, 0, 0, 10, 10, hwnd, (HMENU)99, hinst, NULL);
    if (h) {
        side("len", GetWindowTextLength(h));
        DestroyWindow(h);
    }
    return h != 0;
}
static DWORD c_SetRect(void) { SetRect(RCT(B()), 1, 2, 3, 4); return 0; }
static DWORD c_SetRectEmpty(void) { SetRectEmpty(RCT(B())); return 0; }
static DWORD c_CopyRectD(void) { CopyRect(RCT(B()), &r); return 0; }
static DWORD c_CopyRectS(void)
{
    CopyRect(&r, RCT(B()));
    side("l", r.left);
    side("B()", r.bottom);
    return 0;
}
static DWORD c_IsRectEmpty(void) { return IsRectEmpty(RCT(B())); }
static DWORD c_PtInRect(void) { return PtInRect(RCT(B()), pt); }
static DWORD c_EqualRect(void) { return EqualRect(RCT(B()), &r); }
static DWORD c_IntersectRectD(void) { return IntersectRect(RCT(B()), &r1, &r2); }
static DWORD c_IntersectRectS(void)
{
    DWORD v = IntersectRect(&r, RCT(B()), &r2);
    side("l", r.left);
    return v;
}
static DWORD c_UnionRectS(void)
{
    DWORD v = UnionRect(&r, &r1, RCT(B()));
    side("l", r.left);
    return v;
}
static DWORD c_SubtractRectS(void) { return SubtractRect(&r, &r1, RCT(B())); }
static DWORD c_OffsetRect(void) { OffsetRect(RCT(B()), 1, 1); return 0; }
static DWORD c_InflateRect(void) { InflateRect(RCT(B()), 1, 1); return 0; }
static DWORD c_GetWindowRect(void) { GetWindowRect(hwnd, RCT(B())); return 0; }
static DWORD c_GetClientRect(void) { GetClientRect(hwnd, RCT(B())); return 0; }
static DWORD c_ClientToScreen(void) { ClientToScreen(hwnd, PTS(B())); return 0; }
static DWORD c_ScreenToClient(void) { ScreenToClient(hwnd, PTS(B())); return 0; }
static DWORD c_MapWindowPoints(void) { MapWindowPoints(hwnd, NULL, PTS(B()), 2); return 0; }
static DWORD c_AdjustWindowRect(void) { AdjustWindowRect(RCT(B()), WS_OVERLAPPEDWINDOW, FALSE); return 0; }
static DWORD c_InvalidateRect(void)
{
    ValidateRect(hwnd, NULL);
    InvalidateRect(hwnd, RCT(B()), FALSE);
    side("upd", GetUpdateRect(hwnd, &r, FALSE));
    ValidateRect(hwnd, NULL);
    return 0;
}
static DWORD c_ValidateRect(void)
{
    InvalidateRect(hwnd, NULL, FALSE);
    ValidateRect(hwnd, RCT(B()));
    side("upd", GetUpdateRect(hwnd, &r, FALSE));
    ValidateRect(hwnd, NULL);
    return 0;
}
static DWORD c_GetUpdateRect(void)
{
    DWORD v;
    InvalidateRect(hwnd, NULL, FALSE);
    v = GetUpdateRect(hwnd, RCT(B()), FALSE);
    ValidateRect(hwnd, NULL);
    return v;
}
static DWORD c_RedrawWindow(void)
{
    DWORD v;
    ValidateRect(hwnd, NULL);
    v = RedrawWindow(hwnd, RCT(B()), NULL, RDW_INVALIDATE);
    side("upd", GetUpdateRect(hwnd, &r, FALSE));
    ValidateRect(hwnd, NULL);
    return v;
}
static DWORD c_ScrollWindowR(void) { ScrollWindow(hwnd, 1, 1, RCT(B()), NULL); ValidateRect(hwnd, NULL); return 0; }
static DWORD c_ScrollWindowC(void) { ScrollWindow(hwnd, 1, 1, NULL, RCT(B())); ValidateRect(hwnd, NULL); return 0; }
static DWORD c_ScrollWindowExU(void)
{
    DWORD v = ScrollWindowEx(hwnd, 1, 1, NULL, NULL, NULL, RCT(B()), 0);
    ValidateRect(hwnd, NULL);
    return v;
}
static DWORD c_ScrollDCR(void) { return ScrollDC(hdc, 1, 1, RCT(B()), NULL, NULL, NULL); }
static DWORD c_ScrollDCU(void) { return ScrollDC(hdc, 1, 1, &r, &r, NULL, RCT(B())); }
static DWORD c_FillRect(void) { return FillRect(hdc, RCT(B()), hbr); }
static DWORD c_FrameRect(void) { return FrameRect(hdc, RCT(B()), hbr); }
static DWORD c_InvertRect(void) { InvertRect(hdc, RCT(B())); return 0; }
static DWORD c_DrawFocusRect(void) { DrawFocusRect(hdc, RCT(B())); return 0; }
static DWORD c_GetMessage(void)
{
    DWORD v;
    drain();
    PostMessage(hwnd, WM_USER + 1, 1, 2);
    v = GetMessage((MSG FAR *)B(), hwnd, 0, 0);
    side("left", drain());
    return v;
}
static DWORD c_PeekMessageR(void)
{
    DWORD v;
    drain();
    PostMessage(hwnd, WM_USER + 1, 1, 2);
    v = PeekMessage((MSG FAR *)B(), hwnd, 0, 0, PM_REMOVE);
    side("left", drain());
    return v;
}
static DWORD c_PeekMessageN(void)
{
    DWORD v;
    drain();
    PostMessage(hwnd, WM_USER + 1, 1, 2);
    v = PeekMessage((MSG FAR *)B(), hwnd, 0, 0, PM_NOREMOVE);
    side("left", drain());
    return v;
}
static DWORD c_DispatchMessage(void) { return DispatchMessage((MSG FAR *)B()); }
static DWORD c_TranslateMessage(void) { return TranslateMessage((MSG FAR *)B()); }
static DWORD c_CallMsgFilter(void) { return CallMsgFilter((MSG FAR *)B(), 0); }
static DWORD c_IsDialogMessage(void) { return IsDialogMessage(hdlg, (MSG FAR *)B()); }
static DWORD c_TranslateAccelerator(void) { return TranslateAccelerator(hwnd, hacc, (MSG FAR *)B()); }
static DWORD c_BeginPaint(void)
{
    HDC h;
    PAINTSTRUCT ps;
    InvalidateRect(hwnd, NULL, FALSE);
    h = BeginPaint(hwnd, (PAINTSTRUCT FAR *)B());
    side("upd", GetUpdateRect(hwnd, &r, FALSE));
    if (h) {
        ps.hdc = h;
        EndPaint(hwnd, &ps);
    }
    ValidateRect(hwnd, NULL);
    return h != 0;
}
static DWORD c_EndPaint(void) { EndPaint(hwnd, (PAINTSTRUCT FAR *)B()); return 0; }
static DWORD c_SPIBeep(void) { return SystemParametersInfo(SPI_GETBEEP, 0, B(), 0); }
static DWORD c_SPIKbSpeed(void) { return SystemParametersInfo(SPI_GETKEYBOARDSPEED, 0, B(), 0); }
static DWORD c_SPIIconSp(void) { return SystemParametersInfo(SPI_ICONHORIZONTALSPACING, 0, B(), 0); }
static DWORD c_SPIIconFont(void) { return SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LOGFONT), B(), 0); }
static DWORD c_CreateDialogIndirect(void)
{
    HWND h = CreateDialogIndirect(hinst, B(), hwnd, NULL);
    if (h)
        DestroyWindow(h);
    return h != 0;
}
static DWORD c_AppendMenu(void)
{
    DWORD v = AppendMenu(hmenu, MF_STRING, 100, B());
    side("n", GetMenuItemCount(hmenu));
    DeleteMenu(hmenu, 100, MF_BYCOMMAND);
    return v;
}
static DWORD c_InsertMenu(void)
{
    DWORD v = InsertMenu(hmenu, 0, MF_BYPOSITION | MF_STRING, 101, B());
    side("n", GetMenuItemCount(hmenu));
    DeleteMenu(hmenu, 101, MF_BYCOMMAND);
    return v;
}
static DWORD c_ModifyMenu(void)
{
    DWORD v = ModifyMenu(hmenu, 1, MF_BYCOMMAND | MF_STRING, 1, B());
    side("len", GetMenuString(hmenu, 1, buf, sizeof buf, MF_BYCOMMAND));
    ModifyMenu(hmenu, 1, MF_BYCOMMAND | MF_STRING, 1, "One");
    return v;
}
static DWORD c_GetMenuString(void) { return GetMenuString(hmenu, 1, B(), 10, MF_BYCOMMAND); }
static DWORD c_LoadMenuIndirect(void)
{
    HMENU h = LoadMenuIndirect(B());
    if (h)
        DestroyMenu(h);
    return h != 0;
}
static DWORD c_GetCursorPos(void) { GetCursorPos(PTS(B())); return 0; }
static DWORD c_GetCaretPos(void) { GetCaretPos(PTS(B())); return 0; }
static DWORD c_ClipCursor(void) { ClipCursor(RCT(B())); ClipCursor(NULL); return 0; }
static DWORD c_GetClipCursor(void) { GetClipCursor(RCT(B())); return 0; }
static DWORD c_GetScrollRange1(void) { GetScrollRange(hwnd, SB_VERT, (int FAR *)B(), &iy); side("y", iy); return 0; }
static DWORD c_GetScrollRange2(void) { GetScrollRange(hwnd, SB_VERT, &ix, (int FAR *)B()); side("x", ix); return 0; }
static DWORD c_GetKeyboardState(void) { GetKeyboardState((BYTE FAR *)B()); return 0; }
static DWORD c_SetKeyboardState(void)
{
    static BYTE ks[256];
    GetKeyboardState(ks);
    SetKeyboardState((BYTE FAR *)B());
    side("lb", GetKeyState(VK_LBUTTON) & 0xFF);
    SetKeyboardState(ks);
    return 0;
}
static DWORD c_TabbedTextOutS(void) { return TabbedTextOut(hdc, 0, 0, B(), 8, 0, NULL, 0); }
static DWORD c_TabbedTextOutT(void) { return TabbedTextOut(hdc, 0, 0, "a\tb\tc\td", 7, 4, (int FAR *)B(), 0); }
static DWORD c_GetTabbedTextExtent(void) { return GetTabbedTextExtent(hdc, B(), 8, 0, NULL); }
static DWORD c_GrayString(void) { return GrayString(hdc, hbr, NULL, (LPARAM)B(), 8, 0, 0, 0, 0); }
static DWORD c_SetProp(void) { return SetProp(hwnd, B(), (HANDLE)1); }
static DWORD c_GetProp(void) { return (DWORD)GetProp(hwnd, B()); }
static DWORD c_RemoveProp(void) { return (DWORD)RemoveProp(hwnd, B()); }
static DWORD c_GetClipboardFormatName(void) { return GetClipboardFormatName(fmt, B(), 10); }
static DWORD c_RegisterClipboardFormat(void) { return RegisterClipboardFormat(B()) != 0; }
static DWORD c_EditSetText(void)
{
    DWORD v;
    SetWindowText(hedit, "init");
    v = SendMessage(hedit, WM_SETTEXT, 0, (LPARAM)B());
    side("len", GetWindowTextLength(hedit));
    return v;
}
static DWORD c_EditGetText(void) { SetWindowText(hedit, "edit text"); return SendMessage(hedit, WM_GETTEXT, 10, (LPARAM)B()); }
static DWORD c_EditGetLine(void) { SetWindowText(hedit, "edit text"); return SendMessage(hedit, EM_GETLINE, 0, (LPARAM)B()); }
static DWORD c_EditReplaceSel(void)
{
    DWORD v;
    SetWindowText(hedit, "init");
    v = SendMessage(hedit, EM_REPLACESEL, 0, (LPARAM)B());
    side("len", GetWindowTextLength(hedit));
    return v;
}
static DWORD c_DefSetText(void)
{
    DWORD v;
    SetWindowText(hwnd, "init");
    v = DefWindowProc(hwnd, WM_SETTEXT, 0, (LPARAM)B());
    side("len", GetWindowTextLength(hwnd));
    return v;
}
static DWORD c_DefGetText(void) { SetWindowText(hwnd, "init text"); return DefWindowProc(hwnd, WM_GETTEXT, 10, (LPARAM)B()); }
static DWORD c_LbAddString(void)
{
    DWORD v;
    SendMessage(hlist, LB_RESETCONTENT, 0, 0);
    v = SendMessage(hlist, LB_ADDSTRING, 0, (LPARAM)B());
    side("n", (WORD)SendMessage(hlist, LB_GETCOUNT, 0, 0));
    return v;
}
static DWORD c_LbGetText(void)
{
    SendMessage(hlist, LB_RESETCONTENT, 0, 0);
    SendMessage(hlist, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"list item");
    return SendMessage(hlist, LB_GETTEXT, 0, (LPARAM)B());
}
static DWORD c_LbFindString(void)
{
    SendMessage(hlist, LB_RESETCONTENT, 0, 0);
    SendMessage(hlist, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"abcd");
    return SendMessage(hlist, LB_FINDSTRING, (WPARAM)-1, (LPARAM)B());
}
static DWORD c_CbAddString(void)
{
    DWORD v;
    SendMessage(hcombo, CB_RESETCONTENT, 0, 0);
    v = SendMessage(hcombo, CB_ADDSTRING, 0, (LPARAM)B());
    side("n", (WORD)SendMessage(hcombo, CB_GETCOUNT, 0, 0));
    return v;
}
static DWORD c_CbGetLbText(void)
{
    SendMessage(hcombo, CB_RESETCONTENT, 0, 0);
    SendMessage(hcombo, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"combo item");
    return SendMessage(hcombo, CB_GETLBTEXT, 0, (LPARAM)B());
}
static DWORD c_SetDlgItemText(void)
{
    SetDlgItemText(hdlg, 10, "init");
    SetDlgItemText(hdlg, 10, B());
    side("len", GetWindowTextLength(hedit));
    return 0;
}
static DWORD c_GetDlgItemText(void) { SetDlgItemText(hdlg, 10, "edit text"); return GetDlgItemText(hdlg, 10, B(), 10); }
static DWORD c_GetDlgItemInt(void) { SetDlgItemText(hdlg, 10, "123"); return GetDlgItemInt(hdlg, 10, (BOOL FAR *)B(), FALSE); }
static DWORD c_MapDialogRect(void) { MapDialogRect(hdlg, RCT(B())); return 0; }
static DWORD c_DlgDirSelect(void)
{
    SendMessage(hlist, LB_RESETCONTENT, 0, 0);
    SendMessage(hlist, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"FILE.TXT");
    SendMessage(hlist, LB_SETCURSEL, 0, 0);
    return DlgDirSelect(hdlg, B(), 11);
}
static DWORD c_SetSysColorsI(void)
{
    COLORREF c = GetSysColor(COLOR_BACKGROUND), n = RGB(1, 2, 3);
    int i = COLOR_BACKGROUND;
    SetSysColors(1, (int FAR *)B(), &n);
    side("chg", GetSysColor(COLOR_BACKGROUND) != c);
    SetSysColors(1, &i, &c);
    return 0;
}
static DWORD c_SetSysColorsC(void)
{
    COLORREF c = GetSysColor(COLOR_BACKGROUND);
    int i = COLOR_BACKGROUND;
    SetSysColors(1, &i, (COLORREF FAR *)B());
    side("chg", GetSysColor(COLOR_BACKGROUND) != c);
    SetSysColors(1, &i, &c);
    return 0;
}
static DWORD c_CreateCursor(void)
{
    HCURSOR h = CreateCursor(hinst, 0, 0, 32, 32, B(), bits);
    if (h)
        DestroyCursor(h);
    return h != 0;
}
static DWORD c_CreateIcon(void)
{
    HICON h = CreateIcon(hinst, 32, 32, 1, 1, bits, (BYTE FAR *)B());
    if (h)
        DestroyIcon(h);
    return h != 0;
}
static DWORD c_LoadIcon(void) { return LoadIcon(hinst, B()) != 0; }
static DWORD c_LoadCursor(void) { return LoadCursor(hinst, B()) != 0; }
static DWORD c_LoadBitmap(void) { HBITMAP h = LoadBitmap(hinst, B()); if (h) DeleteObject(h); return h != 0; }
static DWORD c_LoadMenu(void) { HMENU h = LoadMenu(hinst, B()); if (h) DestroyMenu(h); return h != 0; }
static DWORD c_LoadAccelerators(void) { return LoadAccelerators(hinst, B()) != 0; }
static DWORD c_OpenComm(void) { return OpenComm(B(), 1024, 1024); }
static DWORD c_GetWindowPlacement(void) { return GetWindowPlacement(hwnd, (WINDOWPLACEMENT FAR *)B()); }
static DWORD c_SetWindowPlacement(void) { return SetWindowPlacement(hwnd, (WINDOWPLACEMENT FAR *)B()); }

#define KI 0x1F                         /* N F P L E: input */
#define KO 0x3F                         /* and R: output */
enum { RW, RD, RB, RP, RV };
static const struct {
    const char *name;
    DWORD (*fn)(void);
    BYTE kinds, ret;
} cases[] = {
    { "SetWindowText", c_SetWindowText, KI, RV },
    { "GetWindowText", c_GetWindowText, KO, RW },
    { "GetClassName", c_GetClassName, KO, RW },
    { "DrawTextS", c_DrawTextS, KI, RW },
    { "DrawTextN", c_DrawTextN, KI, RW },
    { "DrawTextC", c_DrawTextC, KI, RW },
    { "DrawTextR", c_DrawTextR, KO, RW },
    { "lstrcmp1", c_lstrcmp1, KI, RW },
    { "lstrcmp2", c_lstrcmp2, KI, RW },
    { "lstrcmpi", c_lstrcmpi, KI, RW },
    { "AnsiUpper", c_AnsiUpper, KO, RP },
    { "AnsiLower", c_AnsiLower, KO, RP },
    { "AnsiUpperBuff", c_AnsiUpperBuff, KO, RW },
    { "AnsiNext", c_AnsiNext, KO, RP },
    { "AnsiPrev", c_AnsiPrev, KO, RP },
    { "wsprintfF", c_wsprintfF, KI, RW },
    { "wsprintfO", c_wsprintfO, KO, RW },
    { "wsprintfS", c_wsprintfS, KI, RW },
    { "wvsprintfA", c_wvsprintfA, KI, RW },
    { "wvsprintfF", c_wvsprintfF, KI, RW },
    { "wvsprintfO", c_wvsprintfO, KO, RW },
    { "LoadString", c_LoadString, KO, RW },
    { "GetClassInfoO", c_GetClassInfoO, KO, RW },
    { "GetClassInfoN", c_GetClassInfoN, KI, RW },
    { "FindWindowC", c_FindWindowC, KI, RW },
    { "FindWindowT", c_FindWindowT, KI, RW },
    { "RegisterClassP", c_RegisterClassP, KI, RW },
    { "RegisterClassN", c_RegisterClassN, KI, RW },
    { "RegisterClassM", c_RegisterClassM, KI, RW },
    { "RegisterWindowMessage", c_RegisterWindowMessage, KI, RW },
    { "CreateWindowC", c_CreateWindowC, KI, RW },
    { "CreateWindowT", c_CreateWindowT, KI, RW },
    { "SetRect", c_SetRect, KO, RV },
    { "SetRectEmpty", c_SetRectEmpty, KO, RV },
    { "CopyRectD", c_CopyRectD, KO, RV },
    { "CopyRectS", c_CopyRectS, KI, RV },
    { "IsRectEmpty", c_IsRectEmpty, KI, RW },
    { "PtInRect", c_PtInRect, KI, RW },
    { "EqualRect", c_EqualRect, KI, RW },
    { "IntersectRectD", c_IntersectRectD, KO, RW },
    { "IntersectRectS", c_IntersectRectS, KI, RW },
    { "UnionRectS", c_UnionRectS, KI, RW },
    { "SubtractRectS", c_SubtractRectS, KI, RW },
    { "OffsetRect", c_OffsetRect, KO, RV },
    { "InflateRect", c_InflateRect, KO, RV },
    { "GetWindowRect", c_GetWindowRect, KO, RV },
    { "GetClientRect", c_GetClientRect, KO, RV },
    { "ClientToScreen", c_ClientToScreen, KO, RV },
    { "ScreenToClient", c_ScreenToClient, KO, RV },
    { "MapWindowPoints", c_MapWindowPoints, KO, RV },
    { "AdjustWindowRect", c_AdjustWindowRect, KO, RV },
    { "InvalidateRect", c_InvalidateRect, KI, RV },
    { "ValidateRect", c_ValidateRect, KI, RV },
    { "GetUpdateRect", c_GetUpdateRect, KO, RW },
    { "RedrawWindow", c_RedrawWindow, KI, RW },
    { "ScrollWindowR", c_ScrollWindowR, KI, RV },
    { "ScrollWindowC", c_ScrollWindowC, KI, RV },
    { "ScrollWindowExU", c_ScrollWindowExU, KO, RW },
    { "ScrollDCR", c_ScrollDCR, KI, RW },
    { "ScrollDCU", c_ScrollDCU, KO, RW },
    { "FillRect", c_FillRect, KI, RW },
    { "FrameRect", c_FrameRect, KI, RW },
    { "InvertRect", c_InvertRect, KI, RV },
    { "DrawFocusRect", c_DrawFocusRect, KI, RV },
    { "GetMessage", c_GetMessage, KO, RW },
    { "PeekMessageR", c_PeekMessageR, KO, RW },
    { "PeekMessageN", c_PeekMessageN, KO, RW },
    { "DispatchMessage", c_DispatchMessage, KI, RD },
    { "TranslateMessage", c_TranslateMessage, KI, RW },
    { "CallMsgFilter", c_CallMsgFilter, KI, RW },
    { "IsDialogMessage", c_IsDialogMessage, KI, RW },
    { "TranslateAccelerator", c_TranslateAccelerator, KI, RW },
    { "BeginPaint", c_BeginPaint, KO, RW },
    { "EndPaint", c_EndPaint, KI, RV },
    { "SPIBeep", c_SPIBeep, KO, RW },
    { "SPIKbSpeed", c_SPIKbSpeed, KO, RW },
    { "SPIIconSp", c_SPIIconSp, KO, RW },
    { "SPIIconFont", c_SPIIconFont, KO, RW },
    { "CreateDialogIndirect", c_CreateDialogIndirect, KI, RW },
    { "AppendMenu", c_AppendMenu, KI, RW },
    { "InsertMenu", c_InsertMenu, KI, RW },
    { "ModifyMenu", c_ModifyMenu, KI, RW },
    { "GetMenuString", c_GetMenuString, KO, RW },
    { "LoadMenuIndirect", c_LoadMenuIndirect, KI, RW },
    { "GetCursorPos", c_GetCursorPos, KO, RV },
    { "GetCaretPos", c_GetCaretPos, KO, RV },
    { "ClipCursor", c_ClipCursor, KI, RV },
    { "GetClipCursor", c_GetClipCursor, KO, RV },
    { "GetScrollRange1", c_GetScrollRange1, KO, RV },
    { "GetScrollRange2", c_GetScrollRange2, KO, RV },
    { "GetKeyboardState", c_GetKeyboardState, KO, RV },
    { "SetKeyboardState", c_SetKeyboardState, KI, RV },
    { "TabbedTextOutS", c_TabbedTextOutS, KI, RD },
    { "TabbedTextOutT", c_TabbedTextOutT, KI, RD },
    { "GetTabbedTextExtent", c_GetTabbedTextExtent, KI, RD },
    { "GrayString", c_GrayString, KI, RW },
    { "SetProp", c_SetProp, KI, RW },
    { "GetProp", c_GetProp, KI, RW },
    { "RemoveProp", c_RemoveProp, KI, RW },
    { "GetClipboardFormatName", c_GetClipboardFormatName, KO, RW },
    { "RegisterClipboardFormat", c_RegisterClipboardFormat, KI, RW },
    { "EditSetText", c_EditSetText, KI, RD },
    { "EditGetText", c_EditGetText, KO, RD },
    { "EditGetLine", c_EditGetLine, KO, RD },
    { "EditReplaceSel", c_EditReplaceSel, KI, RD },
    { "DefSetText", c_DefSetText, KI, RD },
    { "DefGetText", c_DefGetText, KO, RD },
    { "LbAddString", c_LbAddString, KI, RD },
    { "LbGetText", c_LbGetText, KO, RD },
    { "LbFindString", c_LbFindString, KI, RD },
    { "CbAddString", c_CbAddString, KI, RD },
    { "CbGetLbText", c_CbGetLbText, KO, RD },
    { "SetDlgItemText", c_SetDlgItemText, KI, RV },
    { "GetDlgItemText", c_GetDlgItemText, KO, RW },
    { "GetDlgItemInt", c_GetDlgItemInt, KO, RW },
    { "MapDialogRect", c_MapDialogRect, KO, RV },
    { "DlgDirSelect", c_DlgDirSelect, KO, RW },
    { "SetSysColorsI", c_SetSysColorsI, KI, RV },
    { "SetSysColorsC", c_SetSysColorsC, KI, RV },
    { "CreateCursor", c_CreateCursor, KI, RW },
    { "CreateIcon", c_CreateIcon, KI, RW },
    { "LoadIcon", c_LoadIcon, KI, RW },
    { "LoadCursor", c_LoadCursor, KI, RW },
    { "LoadBitmap", c_LoadBitmap, KI, RW },
    { "LoadMenu", c_LoadMenu, KI, RW },
    { "LoadAccelerators", c_LoadAccelerators, KI, RW },
    { "OpenComm", c_OpenComm, KI, RW },
    { "GetWindowPlacement", c_GetWindowPlacement, KO, RW },
    { "SetWindowPlacement", c_SetWindowPlacement, KI, RW },
    { 0 }
};

/* where a fault happened: "MODULE:segment", from TOOLHELP */
static void flt_line(void)
{
    GLOBALENTRY ge;
    MODULEENTRY me;
    out("FLT ");
    out(nm);
    out(" int ");
    outhex(flt_int);
    out(" in ");
    ge.dwSize = sizeof ge;
    me.dwSize = sizeof me;
    if (p_GlobalEntryHandle && p_GlobalEntryHandle(&ge, (HGLOBAL)LOWORD(GlobalHandle(flt_cs)))
        && p_ModuleFindHandle && p_ModuleFindHandle(&me, (HMODULE)ge.hOwner)) {
        out(me.szModule);
        out(":");
        outhex(ge.wData);
    } else {
        out("?");
        outhex(flt_cs);
        out(":");
        outhex(flt_ip);
    }
    out(flt_rec ? " recovered" : " not recovered");
    out(flt_n > 1 ? " (more than one)\r\n" : "\r\n");
}

/* the reference value of name, or -1 */
static long refof(const char *name)
{
    WORD i;
    for (i = 0; ref[i].name; i++)
        if (same(ref[i].name, name))
            return ref[i].value;
    return -1;
}

/* WIN.INI [US27] skip=: case names ("Name.K") separated by commas */
static char skip[2048], from[48];
static int explore, started;
static int listed(const char *list, const char *n)
{
    const char *p = list;
    while (*p) {
        int i = 0;
        while (n[i] && p[i] == n[i])
            i++;
        if (!n[i] && (p[i] == ',' || !p[i]))
            return 1;
        while (*p && *p != ',')
            p++;
        if (*p)
            p++;
    }
    return 0;
}

static int ci;
static HGLOBAL hdisc;

/* The emulators the tests run in do not fault on memory through a null
   selector: they use linear address 0, the real-mode interrupt table,
   which a write through NULL overwrites (a later case then ended the
   task, on 3.1 and on w16). It is saved at the start and put back after
   each N case; ".null" is how many of its bytes the call changed (not
   checked: on a processor the access faults). */
static BYTE ivt[1024];
static BYTE FAR *ivt0;

static void ivt_check(void)
{
    WORD i, n = 0;
    for (i = 0; i < sizeof ivt; i++)
        if (ivt0[i] != ivt[i]) {
            ivt0[i] = ivt[i];
            n++;
        }
    if (n) {
        out("NULL ");
        out(nm);
        out(" wrote ");
        outhex(n);
        out(" bytes at 0:0\r\n");
    }
}
static DWORD rv;

static void run_case(void)
{
    char ok[56];
    WORD i;
    name_case(cases[ci].name, ck);
    for (i = 0; nm[i]; i++)
        ok[i] = nm[i];
    ok[i] = '.', ok[i + 1] = 'o', ok[i + 2] = 'k', ok[i + 3] = 0;
    if (!started) {
        if (!same(nm, from))
            return;
        started = 1;
    }
    if (listed(skip, nm) || (!explore && refof(ok) == 0)) {
        out("SKIP ");
        out(nm);
        out("\r\n");
        return;
    }
    out("RUN ");
    out(nm);
    out("\r\n");
    reset();
    if (ck == KP) {                     /* a discarded block's selector */
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE, 64);
        WORD s = SELECTOROF(GlobalLock(h));
        GlobalUnlock(h);
        GlobalDiscard(h);
        hdisc = h;
        np_sel = s | 7;
        bad[KP] = (LPSTR)MAKELP(s, 0);
        side("lar", lar_ah(s));
    }
    if (ck == KE)
        for (i = 0; i < 4; i++)         /* "abcd", no NUL in the block */
            ((LPSTR)MAKELP(esel, eoff))[i] = (char)('a' + i);
    flt_n = 0;
    armed = 1;
    if (Catch(cb) == 0) {
        rv = cases[ci].fn();
        armed = 0;
        obs(ok, 1);
        switch (cases[ci].ret) {
        case RW: obs(nm, LOWORD(rv)); break;
        case RB: obs(nm, rv != 0); break;
        case RD: obs(nm, LOWORD(rv)); side("hi", HIWORD(rv)); break;
        case RP:
            side("off", LOWORD(rv) - OFFSETOF(bad[ck]));
            side("sel", HIWORD(rv) == SELECTOROF(bad[ck]) ? 1 : HIWORD(rv) ? 2 : 0);
            break;
        }
    } else {
        armed = 0;
        obs(ok, 0);
    }
    if (ck == KN)
        ivt_check();
    if (ck == KF)
        side("lar", flar);
    if (flt_n)
        flt_line();
    if (hdisc) {
        np_sel = 0;
        GlobalFree(hdisc);
        hdisc = 0;
    }
    if (ck == KE) {
        side("e0", *(WORD FAR *)MAKELP(esel, eoff));
        side("e1", *(WORD FAR *)MAKELP(esel, eoff + 2));
    }
}

BOOL FAR PASCAL __export __loadds dlg_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    (void)h, (void)m, (void)wp, (void)lp;
    return FALSE;
}

int __cdecl tmain(void)
{
    HGLOBAL he;
    HINSTANCE th = 0;
    OFSTRUCT of;
    WORD i;

    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    out("US27 start\r\n");

    /* TOOLHELP (where the install has it) only names the module a fault
       was in */
    if (OpenFile("TOOLHELP.DLL", &of, OF_EXIST) != HFILE_ERROR)
        th = LoadLibrary("TOOLHELP.DLL");
    if (th > HINSTANCE_ERROR) {
        p_GlobalEntryHandle = (void FAR *)GetProcAddress(th, "GlobalEntryHandle");
        p_ModuleFindHandle = (void FAR *)GetProcAddress(th, "ModuleFindHandle");
    }
    flt_install();
    ivt0 = (BYTE FAR *)MAKELP((WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0000H"), 0);
    for (i = 0; i < sizeof ivt; i++)
        ivt[i] = ivt0[i];

    /* the bad pointers */
    bad[KN] = NULL;                     /* F and P: made for each case (run_case) */
    /* L and E: a selector of limit 1Fh at the start of an 8K block, and R
       a code alias of another block: the emulators the tests run in do not
       fault on every access past a limit or on a write to a code segment,
       and such writes then land in those blocks' room, not in the global
       heap's next arena or the probe's own code (3.1 and w16 alike went
       on to fail in later cases before) */
    he = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 0x2000);
    esel = AllocSelector(SELECTOROF(GlobalLock(he)));
    SetSelectorLimit(esel, 0x1F);
    eoff = lsl_(esel) - 3;
    bad[KL] = (LPSTR)MAKELP(esel, 0x1000);
    bad[KE] = (LPSTR)MAKELP(esel, eoff);
    he = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 0x400);
    bad[KR] = (LPSTR)MAKELP(AllocDStoCSAlias(SELECTOROF(GlobalLock(he))), 0x10);
    obs("E limit ok", eoff + 3 < 0x1000);

    /* what the cases work on */
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "US27";
    RegisterClass(&wc);
    hwnd = CreateWindow("US27", "US27", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_VISIBLE,
                        20, 20, 400, 300, NULL, NULL, hinst, NULL);
    SetScrollRange(hwnd, SB_VERT, 3, 30, FALSE);
    hdlg = CreateDialog(hinst, "D", hwnd, (DLGPROC)dlg_proc);
    hedit = GetDlgItem(hdlg, 10);
    hlist = GetDlgItem(hdlg, 11);
    hcombo = GetDlgItem(hdlg, 12);
    obs("dialog", hdlg && hedit && hlist && hcombo);
    hmenu = CreateMenu();
    AppendMenu(hmenu, MF_STRING, 1, "One");
    AppendMenu(hmenu, MF_STRING, 2, "Two");
    hacc = LoadAccelerators(hinst, "A");
    hbr = GetStockObject(BLACK_BRUSH);
    fmt = RegisterClipboardFormat("US27 format");
    hdc = GetDC(hwnd);
    UpdateWindow(hwnd);
    drain();

    /* WIN.INI [US27] first=, last= (case numbers), kinds= (a mask of
       N F P L E R = 1 2 4 8 16 32): a part of the cases only; skip=: cases
       left out (tests/us27run.sh: those whose fault ended the task on 3.1
       in a way the probe cannot catch); explore=1: run the cases that ended
       the task on 3.1 too (by default those us27ref.h has with .ok 0 are
       skipped: a task's end is nothing for w16 to match); from=Name.K: start
       at that case */
    GetProfileString("US27", "skip", "", skip, sizeof skip);
    explore = GetProfileInt("US27", "explore", 0);
    GetProfileString("US27", "from", "", from, sizeof from);
    started = !from[0];
    {
        int first = GetProfileInt("US27", "first", 0);
        int last = GetProfileInt("US27", "last", 32767);
        int kinds = GetProfileInt("US27", "kinds", 63);
        for (ci = 0; cases[ci].name; ci++)
            if (ci >= first && ci <= last)
                for (ck = 0; ck < NKIND; ck++)
                    if (cases[ci].kinds & kinds & (1 << ck))
                        run_case();
    }

    flt_remove();
    ReleaseDC(hwnd, hdc);
    out(failures ? "US27 end, failures\r\n" : "US27 end\r\n");
    return 0;
}
