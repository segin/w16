/*
 * us4.c - probe US4: a gallery of USER's drawing (ARCHITECTURE.md 10).
 *
 * Run as the shell, it shows one screen after another and saves each as
 * C:\US4_n.BMP (the whole screen through GetDC(NULL) and GetDIBits, as
 * SNAP.EXE does): message boxes of every icon and button set, dialogs with
 * every standard control (us4.rc), the same with the focus moved and a
 * combo box dropped, a window with scroll bars, an open popup menu, a
 * maximised and a minimised window. Modal states are captured from a timer
 * procedure, which then dismisses them. tests/userscreens.sh runs it on
 * Windows 3.1 and on w16's USER and compares the pictures.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HINSTANCE hinst;
static int shot;
static char name[16] = "C:\\US4_00.BMP";

typedef struct { BITMAPINFOHEADER h; RGBQUAD pal[16]; } BMI16;

static void snap(void)
{
    int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN), y;
    WORD stride = (WORD)(((cx * 4 + 31) / 32) * 4);
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy), old;
    HGLOBAL hb = GlobalAlloc(GMEM_MOVEABLE, (DWORD)stride * 32);
    BYTE FAR *buf = (BYTE FAR *)GlobalLock(hb);
    static BMI16 bmi;
    BITMAPFILEHEADER fh;
    HFILE f;
    shot++;
    name[7] = (char)('0' + shot / 10);
    name[8] = (char)('0' + shot % 10);
    old = SelectObject(mem, bm);
    BitBlt(mem, 0, 0, cx, cy, scr, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    bmi.h.biSize = sizeof bmi.h;
    bmi.h.biWidth = cx;
    bmi.h.biHeight = cy;
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = 4;
    bmi.h.biSizeImage = 0;
    for (y = 0; y < cy; y++)
        bmi.h.biSizeImage += stride;
    f = _lcreat(name, 0);
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

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    do {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    } while ((long)(GetTickCount() - end) < 0);
}

/* the timer: snap the modal state, then end it with the key given */
static WORD endkey;
void FAR PASCAL __export __loadds modal_timer(HWND h, UINT m, UINT id, DWORD t)
{
    HWND a = GetActiveWindow();
    KillTimer(NULL, id);
    snap();
    if (endkey == 1) {          /* a popup menu: Esc twice */
        PostMessage(a, WM_KEYDOWN, VK_ESCAPE, 1L);
        PostMessage(a, WM_KEYDOWN, VK_ESCAPE, 1L);
    } else
        PostMessage(GetFocus() ? GetFocus() : a, WM_KEYDOWN, endkey, 1L);
    (void)h, (void)m, (void)t;
}

static void modal_snap(WORD key)
{
    endkey = key;
    SetTimer(NULL, 0, 1500, (TIMERPROC)modal_timer);
}

BOOL FAR PASCAL __export __loadds dlg_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (m) {
    case WM_INITDIALOG:
        if (GetDlgItem(h, 113)) {
            SetDlgItemText(h, 113, "Edit text");
            SetDlgItemText(h, 122, "Disabled");
            CheckDlgButton(h, 104, 1);
            CheckDlgButton(h, 105, 2);
            CheckRadioButton(h, 107, 109, 108);
            SendDlgItemMessage(h, 114, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"alpha");
            SendDlgItemMessage(h, 114, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"beta");
            SendDlgItemMessage(h, 114, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"gamma");
            SendDlgItemMessage(h, 114, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"delta");
            SendDlgItemMessage(h, 114, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"epsilon");
            SendDlgItemMessage(h, 114, LB_SETCURSEL, 1, 0);
            SendDlgItemMessage(h, 115, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"one");
            SendDlgItemMessage(h, 115, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"two");
            SendDlgItemMessage(h, 115, CB_SETCURSEL, 0, 0);
            SendDlgItemMessage(h, 116, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"simple");
            SetScrollRange(GetDlgItem(h, 117), SB_CTL, 0, 100, FALSE);
            SetScrollPos(GetDlgItem(h, 117), SB_CTL, 30, TRUE);
            SetScrollRange(GetDlgItem(h, 118), SB_CTL, 0, 10, FALSE);
            SetScrollPos(GetDlgItem(h, 118), SB_CTL, 10, TRUE);
        } else
            SetDlgItemText(h, 201, "notepad");
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK || wp == IDCANCEL) {
            EndDialog(h, wp);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

LRESULT FAR PASCAL __export __loadds wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        TextOut(dc, 4, 4, "Window text", 11);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HWND w, d;
    HMENU pop;
    FARPROC dp;
    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    dp = MakeProcInstance((FARPROC)dlg_proc, hinst);

    /* 1-4: message boxes */
    modal_snap(VK_RETURN);
    MessageBox(NULL, "A message box with the exclamation icon.", "Caption",
               MB_OK | MB_ICONEXCLAMATION);
    modal_snap(VK_ESCAPE);
    MessageBox(NULL, "Two lines of text,\nand a question.", "Question",
               MB_YESNOCANCEL | MB_ICONQUESTION);
    modal_snap(VK_RETURN);              /* no Cancel button: Esc does nothing */
    MessageBox(NULL, "Stop.", "Hand", MB_ABORTRETRYIGNORE | MB_ICONHAND | MB_DEFBUTTON2);
    modal_snap(VK_ESCAPE);
    MessageBox(NULL, "Information, and a long line of text that has to be wrapped by the message box "
               "because it is wider than the screen allows for a single line.", "Info",
               MB_OKCANCEL | MB_ICONINFORMATION);

    /* 5-6: dialogs */
    modal_snap(VK_ESCAPE);
    DialogBox(hinst, MAKEINTRESOURCE(1), NULL, (DLGPROC)dp);
    modal_snap(VK_ESCAPE);
    DialogBox(hinst, MAKEINTRESOURCE(2), NULL, (DLGPROC)dp);

    /* 7: a modeless dialog with the focus moved twice and the combo box
       dropped */
    d = CreateDialog(hinst, MAKEINTRESOURCE(1), NULL, (DLGPROC)dp);
    pump(500);
    PostMessage(GetFocus(), WM_KEYDOWN, VK_TAB, 1L);
    pump(300);
    PostMessage(GetFocus(), WM_KEYDOWN, VK_TAB, 1L);
    pump(300);
    SendDlgItemMessage(d, 115, CB_SHOWDROPDOWN, TRUE, 0);
    pump(500);
    snap();
    DestroyWindow(d);

    /* 8: a window with scroll bars and a menu */
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US4";
    RegisterClass(&wc);
    w = CreateWindow("US4", "Scroll bars", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
                     40, 30, 300, 200, NULL, NULL, hinst, NULL);
    pop = CreatePopupMenu();
    AppendMenu(pop, MF_STRING, 1, "&Open...\tCtrl+O");
    AppendMenu(pop, MF_STRING | MF_CHECKED, 2, "&Checked");
    AppendMenu(pop, MF_SEPARATOR, 0, NULL);
    AppendMenu(pop, MF_STRING | MF_GRAYED, 3, "&Grayed");
    AppendMenu(pop, MF_STRING, 4, "E&xit");
    {
        HMENU bar = CreateMenu();
        AppendMenu(bar, MF_POPUP, (UINT)pop, "&File");
        AppendMenu(bar, MF_STRING, 10, "&Edit");
        SetMenu(w, bar);
    }
    SetScrollRange(w, SB_VERT, 0, 100, FALSE);
    SetScrollPos(w, SB_VERT, 50, FALSE);
    SetScrollRange(w, SB_HORZ, 0, 10, FALSE);
    ShowWindow(w, SW_SHOWNORMAL);
    UpdateWindow(w);
    pump(500);
    snap();
    /* 9: its File menu open */
    modal_snap(1);
    SendMessage(w, WM_SYSCOMMAND, SC_KEYMENU, 'f');
    pump(300);
    /* 10: its system menu open */
    modal_snap(1);
    SendMessage(w, WM_SYSCOMMAND, SC_KEYMENU, ' ');
    pump(300);
    /* 11: maximised; 12: minimised */
    ShowWindow(w, SW_SHOWMAXIMIZED);
    pump(500);
    snap();
    ShowWindow(w, SW_SHOWMINIMIZED);
    pump(500);
    snap();
    DestroyWindow(w);
    pump(200);
    return 0;
}
