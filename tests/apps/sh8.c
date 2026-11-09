/*
 * sh8.c - SH8: ShellAbout's dialog (each control's class, text, style,
 * place and font, and a screenshot C:\SH8An.BMP), its return value with
 * several kinds of arguments, SHELL's string resources as LoadString
 * returns them, and the layout of SHELL's dialog templates (created with
 * a do-nothing procedure, dumped, C:\SH8Dnnn.BMP). Black-box: only what
 * the running dialogs and LoadString show.
 */
#include <windows.h>

#define TK_REF "sh8ref.h"
#include "tklog.h"
#include "shprobe.h"

static HWND mainw;
static HINSTANCE inst;
static int shot;
static char file[20];
static char buf[300];

void CALLBACK __export Watch(HWND w, UINT m, UINT id, DWORD t)
{
    HWND a = GetActiveWindow();
    (void)m; (void)t;
    KillTimer(w, id);
    if (!a || a == mainw) {
        out("no dialog\r\n");
        return;
    }
    out("DIALOG\r\n");
    {   /* what the system-information lines are made from */
        DWORD fs = GetFreeSpace(0);
        out("freespace=");
        outhex(HIWORD(fs));
        outhex(LOWORD(fs));
        out(" res0=");
        outint(GetFreeSystemResources(0));
        out(" res1=");
        outint(GetFreeSystemResources(1));
        out(" res2=");
        outint(GetFreeSystemResources(2));
        out(" winflags=");
        outhex(LOWORD(GetWinFlags()));
        out("\r\n");
    }
    dump_win(a, a, 0);
    lstrcpy(file, "C:\\SH8A0.BMP");
    file[7] = (char)('0' + shot++);
    snap(file);
    PostMessage(a, WM_COMMAND, IDOK, 0);
}

BOOL CALLBACK __export NullDlg(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    (void)w; (void)wp; (void)lp;
    return m == WM_INITDIALOG;
}

static void about(const char *tag, HWND owner, LPCSTR app, LPCSTR other, HICON icon)
{
    int r;
    out(tag);
    out("\r\n");
    SetTimer(mainw, 1, 1500, (TIMERPROC)MakeProcInstance((FARPROC)Watch, inst));
    r = ShellAbout(owner, app, other, icon);
    obs(tag, (WORD)r);
    pump(300);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HINSTANCE sh;
    WORD i;
    int n;

    inst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(inst);
    out("SH8 ShellAbout\r\n");
    wc.style = 0;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hIcon = LoadIcon(NULL, IDI_ASTERISK);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = inst;
    wc.lpszClassName = "SH8";
    RegisterClass(&wc);
    mainw = CreateWindow("SH8", "SH8 owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 30, 400, 300,
                         NULL, NULL, inst, NULL);
    pump(500);

    about("about.a", mainw, "Probe#Probe Application", "Other stuff", LoadIcon(NULL, IDI_EXCLAMATION));
    about("about.b", mainw, "Plain Title", NULL, NULL);
    about("about.c", NULL, "No owner#Second", "Other", LoadIcon(NULL, IDI_HAND));
    about("about.d", mainw,
          "A very long application title that goes on and on#And the second part is also long, longer than the dialog is wide",
          "Other stuff that is quite long and should wrap or be clipped somewhere in the box, or not",
          LoadIcon(NULL, IDI_APPLICATION));
    about("about.e", mainw, "", "", NULL);
    about("about.f", mainw, "Two##Hashes#Three", "Line\r\nbreak", NULL);
    MoveWindow(mainw, 300, 200, 300, 250, TRUE);
    pump(300);
    about("about.g", mainw, "Moved owner", "x", NULL);

    sh = GetModuleHandle("SHELL");
    for (i = 0; i < 1024; i++) {
        n = LoadString(sh, i, buf, sizeof buf);
        if (n) {
            out("STR ");
            outhex(i);
            out(" ");
            out(buf);
            out("\r\n");
            obsn("str", i, strsum(buf, n));
        }
    }
    for (i = 1; i < 1024; i++) {
        HWND d = CreateDialog(sh, MAKEINTRESOURCE(i), mainw,
                              (DLGPROC)MakeProcInstance((FARPROC)NullDlg, inst));
        if (d) {
            out("TEMPLATE ");
            outhex(i);
            out("\r\n");
            ShowWindow(d, SW_SHOW);
            pump(300);
            dump_win(d, d, 0);
            lstrcpy(file, "C:\\SH8D000.BMP");
            file[7] = (char)('0' + i / 100);
            file[8] = (char)('0' + i / 10 % 10);
            file[9] = (char)('0' + i % 10);
            snap(file);
            DestroyWindow(d);
            pump(200);
        }
    }
    out("SH8 done\r\n");
    return failures;
}
