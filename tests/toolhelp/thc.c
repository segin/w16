/*
 * thc.c - THC.EXE, the child task of the TOOLHELP.DLL probe THP.EXE
 * (tests/toolhelp/thp.c; ARCHITECTURE.md 22). Its command line says what
 * it does:
 *   w    makes a message queue and waits in GetMessage (under two Windows
 *        stack frames, thps-style) until WM_QUIT; exits with its wParam
 *   x    exits at once with code 5
 *   n    as w, with a NotifyRegister callback of its own (after THP's)
 *        that notes what reaches it; writes that to C:\THP.LOG at the end
 *   m    registers a notification callback, waits as w and exits without
 *        NotifyUnRegister
 *   fK   faults (thps.asm do_fault, kind K: d g n u 3 s i); an upper-case
 *        K first registers an interrupt callback of its own that ends the
 *        task (TerminateApp, NO_UAE_BOX); if it goes on: exits with 9
 */
#include <windows.h>
#include <toolhelp.h>

extern WORD __cdecl int_mode, int_skip, int_count, int_rec[16 * 16];
extern DWORD __cdecl int_task_fn, int_term_fn;
extern void __cdecl __far int_cb(void);
extern void __cdecl __far do_fault(WORD kind);
extern WORD __cdecl __far wait_frames(void);
extern WORD __cdecl init_regs[7];    /* start.asm: InitTask's ax bx cx dx si di es */
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static const char logname[] = "C:\\THP.LOG";
static char line[200];

static void lg(const char *fmt, ...)
{
    int n = wvsprintf(line, fmt, (LPSTR)(&fmt + 1));
    HFILE f = _lopen(logname, OF_WRITE);
    line[n++] = '\r';
    line[n++] = '\n';
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    _lwrite(f, line, n);
    _lclose(f);
}

static char seen[8][16];
static WORD nseen;

BOOL __far __pascal __loadds __export ccb(WORD id, DWORD data)
{
    if (id == NFY_OUTSTR && nseen < 8)
        lstrcpyn(seen[nseen++], (LPCSTR)data, 12);
    return FALSE;
}

/* thps.asm's st_run calls this in THP; THC does not trace */
void __cdecl __far st_walk(WORD ss, WORD cs, WORD ip, WORD bp)
{
    (void)ss; (void)cs; (void)ip; (void)bp;
}

/* the message loop, called from wait_frames (thcs.asm) */
WORD __cdecl __far wait_loop(void)
{
    MSG m;
    while (GetMessage(&m, 0, 0, 0))
        ;
    return m.wParam;
}

int __cdecl __far tmain(void)
{
    char FAR *cmd = (char FAR *)MAKELP(((WORD (FAR PASCAL *)(void))GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(37)))(), 0x81);
    HINSTANCE th = 0;
    FARPROC p;
    WORD i, code = 0;
    while (*cmd == ' ')
        cmd++;
    if (cmd[0] == 'x')
        return 5;
    if (cmd[0] == 'n' || cmd[0] == 'm' || (cmd[0] == 'f' && cmd[1] >= 'A' && cmd[1] <= 'Z'))
        th = LoadLibrary("TOOLHELP.DLL");
    switch (cmd[0]) {
    case 'w':
        InitApp((HINSTANCE)init_regs[5]);
        return wait_frames();
    case 'n':
        InitApp((HINSTANCE)init_regs[5]);
        p = MakeProcInstance((FARPROC)ccb, (HINSTANCE)init_regs[5]);
        ((BOOL (FAR PASCAL *)(HTASK, FARPROC, WORD))GetProcAddress(th, MAKEINTRESOURCE(73)))(0, p, NF_NORMAL);
        code = wait_frames();
        ((BOOL (FAR PASCAL *)(HTASK))GetProcAddress(th, MAKEINTRESOURCE(74)))(0);
        for (i = 0; i < nseen; i++)
            lg("chain child %u: \"%s\"", i, (LPSTR)seen[i]);
        return code;
    case 'm':
        p = MakeProcInstance((FARPROC)ccb, (HINSTANCE)init_regs[5]);
        ((BOOL (FAR PASCAL *)(HTASK, FARPROC, WORD))GetProcAddress(th, MAKEINTRESOURCE(73)))(0, p, NF_NORMAL);
        InitApp((HINSTANCE)init_regs[5]);
        return wait_frames();
    case 'f':
        if (cmd[1] >= 'A' && cmd[1] <= 'Z') {
            int_task_fn = (DWORD)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(36));
            int_term_fn = (DWORD)GetProcAddress(th, MAKEINTRESOURCE(77));
            int_mode = 2;
            ((BOOL (FAR PASCAL *)(HTASK, FARPROC))GetProcAddress(th, MAKEINTRESOURCE(75)))(0, (FARPROC)int_cb);
        }
        do_fault(cmd[1] | 0x20);
        lg("child went on after fault %c", cmd[1]);
        return 9;
    }
    return 1;
}
