/*
 * ticons.c - show every icon a file offers through ExtractIcon, in a grid
 * labelled with the icon index (black-box observation of which icons a
 * program has, e.g. Program Manager's stock icons). Logs the count to
 * TK.LOG. Started by SNAP ("run C:\TICONS.EXE file"), it stays up for
 * ten seconds for a screenshot, then exits.
 */
#include <windows.h>
#include <shellapi.h>

extern WORD __cdecl init_regs[7];       /* ax bx cx dx si di es */
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static char file[128] = "PROGMAN.EXE";
static HINSTANCE inst;

static void out(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void num(char *b, WORD v)
{
    char t[6];
    int i = 5, k = 0;
    t[5] = 0;
    do {
        t[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (t[i])
        b[k++] = t[i++];
    b[k] = 0;
}

LRESULT CALLBACK __export IconsWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        WORD n = (WORD)ExtractIcon(inst, file, (UINT)-1), i;
        char b[8];
        for (i = 0; i < n; i++) {
            HICON h = ExtractIcon(inst, file, i);
            int x = 8 + (i % 10) * 60, y = 8 + (i / 10) * 60;
            if ((UINT)h > 1) {
                DrawIcon(dc, x + 10, y, h);
                DestroyIcon(h);
            }
            num(b, i);
            TextOut(dc, x + 22, y + 36, b, lstrlen(b));
        }
        EndPaint(w, &ps);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HWND w;
    MSG m;
    DWORD end;
    char b[8];
    LPSTR tail = (LPSTR)MAKELP(init_regs[6], init_regs[1]);     /* ES:BX */

    WaitEvent(0);
    inst = (HINSTANCE)init_regs[5];
    InitApp(inst);
    while (*tail == ' ')
        tail++;
    if (*tail)
        lstrcpy(file, tail);
    out("TICONS ");
    out(file);
    out(" count=");
    num(b, (WORD)ExtractIcon(inst, file, (UINT)-1));
    out(b);
    out("\r\n");
    wc.style = 0;
    wc.lpfnWndProc = IconsWndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "TIcons";
    RegisterClass(&wc);
    w = CreateWindow("TIcons", file, WS_POPUP | WS_VISIBLE, 0, 0, 640, 480,
                     NULL, NULL, inst, NULL);
    UpdateWindow(w);
    end = GetTickCount() + 10000;
    while ((long)(GetTickCount() - end) < 0) {
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        Yield();
    }
    return 0;
}
