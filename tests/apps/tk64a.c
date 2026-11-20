/*
 * tk64a.c - TK64A.EXE, the helper task of TK64 (tests/apps/tk64.c,
 * KERNEL's FileCdr): what it does is chosen by a letter on its command
 * line (tk64.h). No log of its own: it reports in TK64's record.
 */
#include <windows.h>
#include "tk64.h"

extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */
extern int __cdecl dos_int21(WORD FAR *r);
extern DWORD FAR PASCAL FileCdr(FARPROC proc);           /* KERNEL.130 */

static WORD hex(const char FAR **p)
{
    WORD v = 0;
    const char FAR *s = *p;
    while (*s == ' ')
        s++;
    for (;;) {
        char c = *s;
        if (c >= '0' && c <= '9')
            v = v * 16 + (c - '0');
        else if (c >= 'A' && c <= 'F')
            v = v * 16 + (c - 'A' + 10);
        else
            break;
        s++;
    }
    *p = s;
    return v;
}

/* its own callback (O): never called while it runs */
void FAR PASCAL __loadds own_cb(WORD code, LPSTR path)
{
    (void)code;
    (void)path;
}

static WORD r[8];

static void dos(WORD ax, WORD bx, const char *name)
{
    r[0] = ax;
    r[1] = bx;
    r[2] = 0;
    r[3] = OFFSETOF(name);
    r[4] = r[5] = 0;
    r[6] = SELECTOROF(name);
    r[7] = 0;
    dos_int21(r);
}

/* F: create, close, delete */
static void fileops(void)
{
    static const char name[] = TK64A_FILE;
    dos(0x3C00, 0, name);
    dos(0x3E00, r[0], name);
    dos(0x4100, 0, name);
}

int __cdecl tmain(void)
{
    const char FAR *cmd = MAKELP(init_regs[6], init_regs[1]);
    WORD ds, off, sel;
    tk64sh FAR *sh;
    ds = hex(&cmd);
    off = hex(&cmd);
    sh = (tk64sh FAR *)MAKELP(ds, off);
    sh->task = (WORD)GetCurrentTask();
    while (*cmd == ' ')
        cmd++;
    if (*cmd == 'Q' || *cmd == 'M') {
        /* InitApp (USER.5), if USER is there */
        HMODULE u = GetModuleHandle("USER");
        int (FAR PASCAL *initapp)(HINSTANCE) = u ?
            (int (FAR PASCAL *)(HINSTANCE))GetProcAddress(u, "INITAPP") : NULL;
        if (initapp)
            sh->app = initapp((HINSTANCE)init_regs[5]);
    }
    switch (*cmd) {
    case 'R':
        sh->r1 = FileCdr((FARPROC)own_cb);
        sh->r2 = FileCdr((FARPROC)-1L);
        sh->r3 = FileCdr(NULL);
        fileops();
        break;
    case 'O':
    case 'Q':
        sh->r1 = FileCdr((FARPROC)own_cb);
        break;
    case 'P':
    case 'M':
        cmd++;
        sel = hex(&cmd);
        off = hex(&cmd);
        sh->r1 = FileCdr((FARPROC)MAKELP(sel, off));
        fileops();
        break;
    default:
        fileops();
        break;
    }
    sh->done = 1;
    return 0;
}
