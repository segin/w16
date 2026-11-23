/*
 * dx10.c - DX10.EXE: INT 21h AH=00h (terminate program) from a Windows
 * program (the DOSX audit's H11, ARCHITECTURE.md 9.6). Run
 * as the shell (tests/refrun.sh DX10.EXE; the boot-test case dosxaudit),
 * it starts itself again with the command tail "C". That copy writes
 * C:\DX10C.LOG, ends with INT 21h AH=00h, and if the call returns, says
 * so in the file and ends with 4Ch. The shell waits up to 5 seconds for
 * its module's usage count to drop back to 1, then logs what happened
 * and whether Windows still runs (it ends normally itself).
 * Black-box observation (ARCHITECTURE.md section 3).
 */
#include <windows.h>

#define TK_REF "dx10ref.h"
#include "tklog.h"

static void int21_00(void);
#pragma aux int21_00 = "mov ah, 0" "int 21h" modify [ax];

static int is_child(void)
{
    BYTE FAR *tail = (BYTE FAR *)MAKELP(entry_regs[9], 0x80);
    WORD i;
    for (i = 1; i <= tail[0] && i < 0x7F; i++)
        if (tail[i] == 'C' || tail[i] == 'c')
            return 1;
    return 0;
}

static void note(const char *s, int append)
{
    HFILE f;
    WORD n = 0;
    if (append) {
        f = _lopen("C:\\DX10C.LOG", OF_WRITE);
        if (f != HFILE_ERROR)
            _llseek(f, 0, 2);
    } else
        f = _lcreat("C:\\DX10C.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    while (s[n])
        n++;
    _lwrite(f, s, n);
    _lclose(f);
}

static int has(const char *buf, WORD len, const char *w)
{
    WORD i, j;
    for (i = 0; i < len; i++) {
        for (j = 0; w[j] && i + j < len && buf[i + j] == w[j]; j++)
            ;
        if (!w[j])
            return 1;
    }
    return 0;
}

int __cdecl tmain(void)
{
    HINSTANCE inst = (HINSTANCE)init_regs[5];
    char path[160];
    static char buf[200];
    UINT h;
    DWORD t0;
    HFILE f;
    int n;

    if (is_child()) {
        note("child before\r\n", 0);
        int21_00();
        note("child after\r\n", 1);
        return 7;
    }
    out("DX10 start\r\n");
    GetModuleFileName(inst, path, sizeof path - 3);
    lstrcat(path, " C");
    h = WinExec(path, SW_SHOWNORMAL);
    obs("winexec_ok", h > 32);
    t0 = GetTickCount();
    while (GetTickCount() - t0 < 5000) {
        MSG m;
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        if (GetModuleUsage(inst) < 2)
            break;
    }
    obs("child_ended", GetModuleUsage(inst) < 2);
    n = 0;
    f = _lopen("C:\\DX10C.LOG", OF_READ);
    if (f != HFILE_ERROR) {
        n = _lread(f, buf, sizeof buf);
        _lclose(f);
        if (n < 0)
            n = 0;
    }
    obs("child_before", has(buf, n, "before"));
    obs("child_after", has(buf, n, "after"));
    out("DX10 done\r\n");
    return 0;
}
