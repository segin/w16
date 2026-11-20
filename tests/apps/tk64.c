/*
 * tk64.c - KERNEL's file-system change callback, FileCdr (KERNEL.130;
 * Schulman, "Undocumented Windows", ch. 5: one callback, owned by the
 * task that installed it; -1 asks for it; File Manager's way of seeing
 * the file system change). Observed on Windows 3.1 (tests/tk64ref.sh),
 * checked against w16's KERNEL in make test.
 *
 * It makes C:\TK64D, goes there, installs its callback (cb_entry,
 * tk64s.asm) and does one file operation after another, each through
 * INT 21h with the registers it chooses ("3C rel" ... below, the AX of
 * the call), and through KERNEL's file APIs and DOS3Call. The callback
 * records each call: the operation it came in (the label), the code,
 * the path (text, a hash, whether it is the program's own string),
 * whether the path exists then (DOS FindFirst: before or after the
 * change), the task it runs in, and its depth (nested in itself). For
 * every label TK.LOG gets the count and each call's details, and what
 * the INT 21h returned: the carry, and whether AX, BX, CX or DX came
 * back as the callback left them (it returns cb_ret in AX and BEEFh in
 * BX, CX and DX). Then:
 *  - "ret": what the callback's return value does (0 and 1 for a delete)
 *  - "nest": the callback itself creates and deletes a file
 *  - "again": the same task installs a second callback
 *  - "helper R": TK64A (tk64a.c) tries FileCdr while TK64 holds it, then
 *    creates and deletes a file (the callback runs in its context)
 *  - "unreg": FileCdr(NULL) by the owner
 *  - "helper Q" (only with USER): TK64A calls InitApp, installs its own
 *    callback and ends: USER's AppExit removes it
 * A callback another task leaves behind cannot be removed, so each of
 * these runs in a Windows of its own, as a copy of TK64.EXE whose name
 * picks it (the Makefile makes them):
 *  - TK64M.EXE, "helper M" (only with USER): TK64A calls InitApp,
 *    installs TK64's callback, works and ends: it stays (not TK64A's
 *    module's), TK64 cannot remove it, and it is called
 *  - TK64O.EXE, "helper O": TK64A installs its own callback and ends
 *    without InitApp: it stays until the next change, which removes it
 *    (its code is gone); 3.1 in 386 Enhanced mode ends TK64 there
 *  - TK64P.EXE, "helper P": as M without InitApp
 * The "== ..." lines mark the steps; every observation is written
 * at the end (TK.LOG's writes, 40h and 68h, call nothing).
 *
 * Runs in make test beside TK62 (tests/run-tests.sh: TK64, TK64O and
 * TK64P, without USER), and with Windows in the boot-test case filecdr;
 * the reference runs: tests/tk64ref.sh.
 */
#include <windows.h>

#define TK_REF "tk64ref.h"
#include "tklog.h"
#include "tk64.h"

extern int __cdecl dos_int21(WORD FAR *r);
extern int __cdecl dos3call_regs(WORD FAR *r);
extern int __cdecl nohook_regs(WORD FAR *r);
extern WORD __cdecl ptr_ok(WORD sel);
extern WORD __cdecl cur_ds(void);
extern void FAR PASCAL cb_entry(WORD code, LPSTR path);
extern void FAR PASCAL cb2_entry(WORD code, LPSTR path);
extern WORD cb2_count;
extern DWORD FAR PASCAL FileCdr(FARPROC proc);           /* KERNEL.130 */

/* ---- the operations ---- */

enum {
    L_IDLE, L_3C, L_40, L_5701, L_5700, L_3E, L_3D0, L_3E0, L_3D1, L_40B,
    L_3EB, L_3D2, L_3EC, L_4300, L_4301, L_4301M, L_56, L_41, L_41M, L_39,
    L_3B, L_3BUP, L_3A, L_3AM, L_5B, L_3E5B, L_5BX, L_5A, L_3E5A, L_415A,
    L_6C, L_3E6C, L_6CO, L_3E6CO, L_3CF, L_3ECF, L_3CS, L_3ECS, L_3CL,
    L_3ECL, L_41L, L_3CD, L_3ECD, L_3CX, L_D3C, L_3ED3, L_4E, L_47, L_3DF,
    L_68, L_45, L_3E45, L_3EF, L_LCREAT, L_LWRITE, L_LCLOSE, L_LOPENW, L_LCLOSE2,
    L_OFCREATE, L_LCLOSE3, L_OFEXIST, L_OFPARSE, L_OFDELETE, L_FCB16,
    L_FCB10, L_FCB13, L_RETPREP, L_RET0, L_RET1, L_NEST, L_NESTDEL,
    L_CB2, L_CB2DEL, L_3CAL, L_3CROOT, L_3CLD, L_3CUP, L_3CATROOT,
    L_41ATROOT, L_6CR, L_41FULL, L_CLOSE, L_NH3C, L_HR, L_HQ, L_HM, L_HO,
    L_HP,
    L_AQ, L_AM, L_AO, L_AP, L_CLEANUP, L_COUNT
};

static const char *const label[L_COUNT] = {
    "idle", "3C rel", "40 write", "5701 set time", "5700 get time",
    "3E close", "3D00 read", "3E close read", "3D01 write", "40 write 2",
    "3E close write", "3D02 rw", "3E close rw", "4300 get attr",
    "4301 set attr", "4301 missing", "56 rename", "41 delete",
    "41 missing", "39 mkdir", "3B chdir", "3B chdir up", "3A rmdir",
    "3A missing", "5B new", "3E close new", "5B exists", "5A unique",
    "3E close unique", "41 unique", "6C create", "3E close 6C",
    "6C open", "3E close 6C open", "3C full", "3E close full",
    "3C slashes", "3E close slashes", "3C long", "3E close long",
    "41 long", "3C drive rel", "3E close drive rel", "3C no dir",
    "DOS3Call 3C", "3E close DOS3Call", "4E find", "47 cwd",
    "3D02 to commit", "68 commit", "45 dup", "3E close dup",
    "3E close committed", "_lcreat", "_lwrite", "_lclose",
    "_lopen write", "_lclose 2", "OpenFile create", "_lclose 3",
    "OpenFile exist", "OpenFile parse", "OpenFile delete", "FCB 16 create",
    "FCB 10 close", "FCB 13 delete", "ret prep", "ret 0 delete",
    "ret 1 delete", "nest 3C", "nest 41", "cb2 3C", "cb2 41",
    "3C AL=55", "3C root rel", "3C lower drive", "3C dotdot",
    "3C at root", "41 at root", "6C replace", "41 full", "3E close other",
    "NoHookDOSCall 3C",
    "helper R", "helper Q", "helper M", "helper O", "helper P",
    "after Q", "after M", "after O", "after P", "cleanup"
};

static struct {
    WORD cf, ax, clob;
} res[L_COUNT];

/* ---- the callback (cb_entry calls cb_c) ---- */

#define MAXEV 140
#define PATHN 80

typedef struct ev {
    WORD op, code, depth, task, exists, same, ptrok;
    char path[PATHN];
} ev;

static ev evs[MAXEV];
static WORD nev, lost;
static WORD cur_op;
static const char FAR *given;   /* the operation's own path string */
static WORD self_task;
static tk64sh sh;
static WORD cb_depth;
static WORD nest_mode;
static WORD have_user;          /* USER is loaded: the InitApp cases */
static char scenario;           /* TK64x.EXE: x */
static WORD named5a;            /* 5Ah gave the name back */
WORD cb_ret = 0x5A5A;

static WORD rr[8];              /* the callback's own INT 21h calls */
static BYTE dta[64];

static void cb_dos(WORD ax, WORD bx, WORD cx, const char FAR *name)
{
    rr[0] = ax;
    rr[1] = bx;
    rr[2] = cx;
    rr[3] = OFFSETOF(name);
    rr[4] = rr[5] = 0;
    rr[6] = SELECTOROF(name);
    rr[7] = 0;
    dos_int21(rr);
}

/* does the file (or directory) exist now? FindFirst with a DTA of our
   own (put back afterwards) */
static WORD exists(const char FAR *name)
{
    WORD old_es, old_bx, found;
    rr[0] = 0x2F00;
    rr[6] = rr[7] = 0;
    dos_int21(rr);
    old_es = rr[7];
    old_bx = rr[1];
    rr[0] = 0x1A00;
    rr[3] = OFFSETOF((BYTE FAR *)dta);
    rr[6] = SELECTOROF((BYTE FAR *)dta);
    rr[7] = 0;
    dos_int21(rr);
    rr[0] = 0x4E00;
    rr[2] = 0x37;
    rr[3] = OFFSETOF(name);
    rr[6] = SELECTOROF(name);
    rr[7] = 0;
    found = !dos_int21(rr);
    rr[0] = 0x1A00;
    rr[3] = old_bx;
    rr[6] = old_es;
    rr[7] = 0;
    dos_int21(rr);
    return found;
}

static const char nest_name[] = "C:\\TK64D\\NEST.TXT";

void __cdecl cb_c(WORD code, char FAR *path)
{
    WORD d = cb_depth++, i;
    ev *e;
    if (nev >= MAXEV) {
        lost++;
        cb_depth--;
        return;
    }
    e = &evs[nev++];
    e->op = cur_op;
    e->code = code;
    e->depth = d;
    e->task = (WORD)GetCurrentTask();
    e->task = e->task == self_task ? 1 : e->task == sh.task ? 2 : 0;
    e->same = path == given;
    e->ptrok = SELECTOROF(path) && ptr_ok(SELECTOROF(path));
    e->path[0] = 0;
    e->exists = 0xFF;
    if (e->ptrok) {
        for (i = 0; i < PATHN - 1 && path[i]; i++)
            e->path[i] = path[i];
        e->path[i] = 0;
        e->exists = exists(path);
    }
    if (nest_mode && !d) {
        cb_dos(0x3C00, 0, 0, nest_name);
        cb_dos(0x3E00, rr[0], 0, nest_name);
        cb_dos(0x4100, 0, 0, nest_name);
    }
    cb_depth--;
}

/* ---- running an operation ---- */

static WORD R[8];               /* ax bx cx dx si di ds es */

static void setr(WORD ax, WORD bx, WORD cx, const char FAR *name)
{
    R[0] = ax;
    R[1] = bx;
    R[2] = cx;
    R[3] = OFFSETOF(name);
    R[4] = R[5] = 0;
    R[6] = name ? SELECTOROF(name) : cur_ds();
    R[7] = 0;
}

static void finish(WORD lab, WORD cf)
{
    res[lab].cf = cf;
    res[lab].ax = R[0];
    res[lab].clob = (R[0] == cb_ret ? 8 : 0) | (R[1] == 0xBEEF ? 1 : 0) |
                    (R[2] == 0xBEEF ? 2 : 0) | (R[3] == 0xBEEF ? 4 : 0);
    cur_op = L_IDLE;
    given = 0;
}

/* INT 21h with R, as operation lab; the path is DS:DX (or DS:SI) */
static WORD run(WORD lab)
{
    cur_op = lab;
    given = MAKELP(R[6], (R[0] >> 8) == 0x6C ? R[4] : R[3]);
    finish(lab, dos_int21(R));
    return R[0];
}

static WORD op(WORD lab, WORD ax, WORD bx, WORD cx, const char FAR *name)
{
    setr(ax, bx, cx, name);
    return run(lab);
}

static void opclose(WORD lab, WORD h)
{
    op(lab, 0x3E00, h, 0, NULL);
}

/* ---- output ---- */

static char nm[96];

static const char *name2(const char *a, const char *b)
{
    WORD n = 0;
    while (*a && n < sizeof nm - 1)
        nm[n++] = *a++;
    while (*b && n < sizeof nm - 1)
        nm[n++] = *b++;
    nm[n] = 0;
    return nm;
}

static const char *name3(const char *a, WORD k, const char *b)
{
    char num[4];
    num[0] = '#';
    num[1] = (char)('0' + k);
    num[2] = 0;
    name2(a, num);
    lstrcat(nm, b);
    return nm;
}

static WORD hash(const char *s)
{
    WORD h = 0;
    while (*s)
        h = h * 31 + (BYTE)*s++;
    return h;
}

/* the generated name of 5Ah and anything in TEMP vary: the path is noted
   but not checked */
static int varying(WORD lab)
{
    return lab == L_5A || lab == L_3E5A || lab == L_415A || lab == L_IDLE ||
           lab == L_CLEANUP;
}

/* is the label this run's? TK64M, O, P: their helper's and the
   cleanup's; TK64: all others */
static int mine(WORD lab)
{
    switch (scenario) {
    case 'M':
        return lab == L_HM || lab == L_AM || lab == L_CLEANUP || lab == L_IDLE;
    case 'O':
        return lab == L_HO || lab == L_AO || lab == L_CLEANUP || lab == L_IDLE;
    case 'P':
        return lab == L_HP || lab == L_AP || lab == L_CLEANUP || lab == L_IDLE;
    }
    return lab != L_HM && lab != L_AM && lab != L_HO && lab != L_AO &&
           lab != L_HP && lab != L_AP;
}

static void report(WORD lab)
{
    WORD i, k = 0, n = 0;
    void (*ob)(const char *, WORD) = obs;
    /* (HDPMI16 does not give 5Ah's name back: what follows then is not
       KERNEL's doing) */
    if ((lab == L_5A || lab == L_3E5A || lab == L_415A) && !named5a)
        ob = obs_env;
    for (i = 0; i < nev; i++)
        if (evs[i].op == lab)
            n++;
    if ((lab == L_HQ || lab == L_AQ) && !have_user)
        return;                 /* (InitApp: only with USER) */
    if (!mine(lab))
        return;                 /* another run's */
    if (lab == L_IDLE || lab == L_CLEANUP)
        obs_env(name2(label[lab], ": n"), n);
    else
        ob(name2(label[lab], ": n"), n);
    if (lab != L_IDLE && (lab < L_HR || lab > L_HP) && lab != L_CLEANUP) {
        if (lab >= L_FCB16 && lab <= L_FCB13)   /* (the DOS extender's) */
            obs_env(name2(label[lab], ": cf"), res[lab].cf);
        else
            ob(name2(label[lab], ": cf"), res[lab].cf);
        ob(name2(label[lab], ": clobbered"), res[lab].clob);
    }
    for (i = 0; i < nev; i++) {
        ev *e = &evs[i];
        if (e->op != lab)
            continue;
        k++;
        if (k > 9)
            break;
        ob(name3(label[lab], k, " code"), e->code);
        obs_str(name3(label[lab], k, " path"), e->path);
        if (varying(lab))
            obs_env(name3(label[lab], k, " path hash"), hash(e->path));
        else
            ob(name3(label[lab], k, " path hash"), hash(e->path));
        ob(name3(label[lab], k, " own string"), e->same);
        ob(name3(label[lab], k, " exists"), e->exists);
        ob(name3(label[lab], k, " task"), e->task);
        ob(name3(label[lab], k, " depth"), e->depth);
    }
}

/* ---- the helper task ---- */

static char helper[160];

static void hex4(char *p, WORD v)
{
    int i;
    for (i = 3; i >= 0; i--) {
        p[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
}

/* run TK64A with the letter (and more); wait for its end */
static void run_helper(const char *more, WORD lab)
{
    WORD n, i, t;
    char cmd[200];
    n = GetModuleFileName((HINSTANCE)(entry_regs[E_DS] & ~1u), helper, sizeof helper - 12);
    for (i = n; i && helper[i - 1] != '\\'; i--)
        ;
    lstrcpy(helper + i, "TK64A.EXE");
    lstrcpy(cmd, helper);
    n = lstrlen(cmd);
    cmd[n++] = ' ';
    hex4(cmd + n, cur_ds());
    cmd[n + 4] = ' ';
    hex4(cmd + n + 5, (WORD)(void NEAR *)&sh);
    cmd[n + 9] = ' ';
    cmd[n + 10] = 0;
    lstrcat(cmd, more);
    sh.done = 0;
    sh.task = 0;
    sh.app = 0;
    sh.r1 = sh.r2 = sh.r3 = 0xDEADDEADUL;
    cur_op = lab;                       /* (3.1's WinExec runs it at once) */
    t = WinExec(cmd, SW_SHOWNORMAL);
    res[lab].cf = t <= 32;
    for (i = 0; i < 2000 && !sh.done; i++)
        Yield();
    for (i = 0; i < 2000 && sh.task && IsTask((HTASK)sh.task); i++)
        Yield();
    cur_op = L_IDLE;
}

/* ---- the probe ---- */

static OFSTRUCT of;
static BYTE fcb[44];
static char ubuf[80];

static void setfcb(void)
{
    static const char n[12] = "C64FCB  TXT";
    WORD i;
    for (i = 0; i < sizeof fcb; i++)
        fcb[i] = 0;
    fcb[0] = 0;                 /* the current drive */
    for (i = 0; i < 11; i++)
        fcb[1 + i] = n[i];
}

static void ops(void)
{
    WORD h;
    static const char wbuf[] = "hello";
    /* create, write, set and get the time, close */
    h = op(L_3C, 0x3C00, 0, 0, "c64a.txt");
    setr(0x4000, h, 5, wbuf);
    run(L_40);
    op(L_5701, 0x5701, h, 0x6000, (const char FAR *)MAKELP(cur_ds(), 0x2C21));
    op(L_5700, 0x5700, h, 0, (const char FAR *)MAKELP(cur_ds(), 0));
    opclose(L_3E, h);
    /* open to read, to write, read-write */
    h = op(L_3D0, 0x3D00, 0, 0, "c64a.txt");
    opclose(L_3E0, h);
    h = op(L_3D1, 0x3D01, 0, 0, "C:\\TK64D\\C64A.TXT");
    setr(0x4000, h, 5, wbuf);
    run(L_40B);
    opclose(L_3EB, h);
    h = op(L_3D2, 0x3D02, 0, 0, "c64a.txt");
    opclose(L_3EC, h);
    /* attributes */
    op(L_4300, 0x4300, 0, 0, "c64a.txt");
    op(L_4301, 0x4301, 0, 0, "c64a.txt");
    op(L_4301M, 0x4301, 0, 0, "nofile.txt");
    /* rename, delete */
    setr(0x5600, 0, 0, "c64a.txt");
    R[5] = OFFSETOF((const char FAR *)"c64b.txt");
    R[7] = SELECTOROF((const char FAR *)"c64b.txt");
    run(L_56);
    op(L_41, 0x4100, 0, 0, "c64b.txt");
    op(L_41M, 0x4100, 0, 0, "c64b.txt");
    /* directories */
    op(L_39, 0x3900, 0, 0, "sub64");
    op(L_3B, 0x3B00, 0, 0, "sub64");
    op(L_3BUP, 0x3B00, 0, 0, "..");
    op(L_3A, 0x3A00, 0, 0, "sub64");
    op(L_3AM, 0x3A00, 0, 0, "sub64");
    /* create new, unique */
    h = op(L_5B, 0x5B00, 0, 0, "c64n.txt");
    opclose(L_3E5B, h);
    op(L_5BX, 0x5B00, 0, 0, "c64n.txt");
    lstrcpy(ubuf, "C:\\TK64D\\");
    h = op(L_5A, 0x5A00, 0, 0, ubuf);
    named5a = lstrlen(ubuf) > 9;
    opclose(L_3E5A, h);
    op(L_415A, 0x4100, 0, 0, ubuf);
    /* extended open: create if missing; open if there */
    setr(0x6C00, 0x0002, 0, NULL);
    R[3] = 0x0010;
    R[4] = OFFSETOF((const char FAR *)"c64x.txt");
    R[6] = SELECTOROF((const char FAR *)"c64x.txt");
    h = run(L_6C);
    opclose(L_3E6C, h);
    setr(0x6C00, 0x0000, 0, NULL);
    R[3] = 0x0001;
    R[4] = OFFSETOF((const char FAR *)"c64x.txt");
    R[6] = SELECTOROF((const char FAR *)"c64x.txt");
    h = run(L_6CO);
    opclose(L_3E6CO, h);
    /* the spellings of a path */
    h = op(L_3CF, 0x3C00, 0, 0, "C:\\TK64D\\C64F.TXT");
    opclose(L_3ECF, h);
    h = op(L_3CS, 0x3C00, 0, 0, "c:/TK64D/../tk64d/./c64g.txt");
    opclose(L_3ECS, h);
    h = op(L_3CL, 0x3C00, 0, 0, "longname64.text");
    opclose(L_3ECL, h);
    op(L_41L, 0x4100, 0, 0, "longname.tex");
    h = op(L_3CD, 0x3C00, 0, 0, "C:c64h.txt");
    opclose(L_3ECD, h);
    op(L_3CX, 0x3C00, 0, 0, "nodir\\x.txt");
    /* DOS3Call */
    setr(0x3C00, 0, 0, "c64d.txt");
    cur_op = L_D3C;
    given = MAKELP(R[6], R[3]);
    finish(L_D3C, dos3call_regs(R));
    opclose(L_3ED3, R[0]);
    /* NoHookDOSCall (KERNEL.101): past KERNEL's hook */
    setr(0x3C00, 0, 0, "c64u.txt");
    cur_op = L_NH3C;
    given = MAKELP(R[6], R[3]);
    finish(L_NH3C, nohook_regs(R));
    opclose(L_CLOSE, R[0]);
    /* calls that change nothing */
    {
        static BYTE fdta[64];
        setr(0x1A00, 0, 0, (const char FAR *)fdta);
        dos_int21(R);
    }
    op(L_4E, 0x4E00, 0, 0x37, "*.*");
    setr(0x4700, 0, 0, ubuf);
    R[3] = 3;
    R[4] = OFFSETOF((char FAR *)ubuf);
    run(L_47);
    h = op(L_3DF, 0x3D02, 0, 0, "c64f.txt");
    op(L_68, 0x6800, h, 0, NULL);
    opclose(L_3E45, op(L_45, 0x4500, h, 0, NULL));
    opclose(L_3EF, h);
    /* KERNEL's file APIs */
    cur_op = L_LCREAT;
    h = _lcreat("c64l.txt", 0);
    res[L_LCREAT].cf = h == HFILE_ERROR;
    cur_op = L_LWRITE;
    res[L_LWRITE].cf = _lwrite(h, wbuf, 5) != 5;
    cur_op = L_LCLOSE;
    res[L_LCLOSE].cf = _lclose(h) != 0;
    cur_op = L_LOPENW;
    h = _lopen("c64l.txt", OF_WRITE);
    res[L_LOPENW].cf = h == HFILE_ERROR;
    cur_op = L_LCLOSE2;
    _lclose(h);
    cur_op = L_OFCREATE;
    h = OpenFile("c64o.txt", &of, OF_CREATE);
    res[L_OFCREATE].cf = h == HFILE_ERROR;
    cur_op = L_LCLOSE3;
    _lclose(h);
    cur_op = L_OFEXIST;
    res[L_OFEXIST].cf = OpenFile("c64o.txt", &of, OF_EXIST) == HFILE_ERROR;
    cur_op = L_OFPARSE;
    res[L_OFPARSE].cf = OpenFile("c64p.txt", &of, OF_PARSE) == HFILE_ERROR;
    cur_op = L_OFDELETE;
    res[L_OFDELETE].cf = OpenFile("c64o.txt", &of, OF_DELETE) == HFILE_ERROR;
    cur_op = L_IDLE;
    /* more spellings: AL, a path from the root, a lower-case drive, "..",
       the root as the current directory; 6Ch replacing a file; a delete
       by its full path */
    opclose(L_CLOSE, op(L_3CAL, 0x3C55, 0, 0, "c64e.txt"));
    opclose(L_CLOSE, op(L_3CROOT, 0x3C00, 0, 0, "\\TK64D\\c64i.txt"));
    opclose(L_CLOSE, op(L_3CLD, 0x3C00, 0, 0, "c:\\tk64d\\c64j.txt"));
    opclose(L_CLOSE, op(L_3CUP, 0x3C00, 0, 0, "..\\TK64D\\c64m.txt"));
    setr(0x3B00, 0, 0, "C:\\");
    dos_int21(R);
    opclose(L_CLOSE, op(L_3CATROOT, 0x3C00, 0, 0, "c64t.txt"));
    op(L_41ATROOT, 0x4100, 0, 0, "c64t.txt");
    setr(0x3B00, 0, 0, "C:\\TK64D");
    dos_int21(R);
    setr(0x6C00, 0x0002, 0, NULL);
    R[3] = 0x0012;
    R[4] = OFFSETOF((const char FAR *)"c64x.txt");
    R[6] = SELECTOROF((const char FAR *)"c64x.txt");
    opclose(L_CLOSE, run(L_6CR));
    op(L_41FULL, 0x4100, 0, 0, "C:\\TK64D\\C64X.TXT");
    /* FCB create, close, delete (Standard mode's DOSX does not translate
       them: AL = FFh) */
    setfcb();
    op(L_FCB16, 0x1600, 0, 0, (const char FAR *)fcb);
    op(L_FCB10, 0x1000, 0, 0, (const char FAR *)fcb);
    setfcb();
    op(L_FCB13, 0x1300, 0, 0, (const char FAR *)fcb);
    res[L_FCB16].cf = res[L_FCB16].ax & 0xFF;
    res[L_FCB10].cf = res[L_FCB10].ax & 0xFF;
    res[L_FCB13].cf = res[L_FCB13].ax & 0xFF;
}

/* ---- observations kept until the end (nothing is written while the
   callback is installed) ---- */

#define MAXD 120
static struct {
    WORD name, v, env;
} dl[MAXD];
static char dpool[3000];
static WORD nd, dpos;

static void dobs_(const char *a, const char *b, WORD v, WORD env)
{
    if (nd >= MAXD || dpos + 80 > sizeof dpool)
        return;
    dl[nd].name = dpos;
    while (*a)
        dpool[dpos++] = *a++;
    while (*b)
        dpool[dpos++] = *b++;
    dpool[dpos++] = 0;
    dl[nd].v = v;
    dl[nd++].env = env;
}

static void dobs(const char *a, const char *b, WORD v)
{
    dobs_(a, b, v, 0);
}

/* a FileCdr result: the low word checked, the high word noted */
static void dobsd(const char *a, const char *b, DWORD v)
{
    dobs_(a, b, LOWORD(v), 0);
    dobs_(a, name2(b, " (high word)"), HIWORD(v), 1);
}

static void flush(void)
{
    WORD i;
    for (i = 0; i < nd; i++)
        if (dl[i].env)
            obs_env(dpool + dl[i].name, dl[i].v);
        else
            obs(dpool + dl[i].name, dl[i].v);
}

static FARPROC cb;
static DWORD cbv;

/* what FileCdr(-1) gives: 0 none, 1 ours, 2 another */
static WORD who(void)
{
    DWORD v = FileCdr((FARPROC)-1L);
    return !v ? 0 : v == cbv ? 1 : 2;
}

static void step(const char *name)
{
    out("== ");
    out(name);
    out("\r\n");
}

/* TK64A installs a callback (its own, or ours: P and M) and ends,
   having called InitApp (Q and M: then USER sees it end) or not */
static void ended(char letter, WORD lab, WORD after)
{
    char more[16], tag[12];
    WORD w, h;
    lstrcpy(tag, "helper ?: ");
    tag[7] = letter;
    step(tag);
    dobs(tag, "none before", who());
    more[0] = letter;
    more[1] = 0;
    if (letter == 'P' || letter == 'M') {
        hex4(more + 1, SELECTOROF(cb));
        more[5] = ' ';
        hex4(more + 6, OFFSETOF(cb));
        more[10] = 0;
    }
    run_helper(more, lab);
    dobs(tag, "started", !res[lab].cf);
    dobs(tag, "done", sh.done);
    dobs(tag, "InitApp", sh.app != 0);
    dobsd(tag, "FileCdr", sh.r1);
    dobs(tag, "ended", sh.task && !IsTask((HTASK)sh.task));
    dobs(tag, "TK64A still loaded", GetModuleHandle("TK64A") != 0);
    w = who();
    dobs(tag, "-1 after its end", w);
    if (!w)
        return;
    dobsd(tag, "NULL by us", FileCdr(NULL));
    w = who();
    dobs(tag, "-1 then", w);
    if (!w)
        return;
    dobs(tag, "its selector valid", ptr_ok(SELECTOROF(FileCdr((FARPROC)-1L))));
    /* a file operation of ours, with its callback still installed (3.1
       in 386 Enhanced mode ends here with TK64O's: its code is gone) */
    step("an operation with it installed");
    h = op(after, 0x3C00, 0, 0, "c64k.txt");
    opclose(after, h);
    op(after, 0x4100, 0, 0, "c64k.txt");
    dobs(tag, "-1 after an operation", who());
    dobsd(tag, "NULL after an operation", FileCdr(NULL));
    dobs(tag, "-1 at last", who());
}

/* the operations, and FileCdr from this task and another */
static void main_run(void)
{
    WORD h;
    FARPROC cb2 = (FARPROC)cb2_entry;
    step("operations");
    dobsd("", "FileCdr(cb)", FileCdr(cb));
    dobs("", "FileCdr(-1) is cb", who() == 1);
    ops();

    /* the callback's result: 0, then 1, for a delete */
    step("ret");
    h = op(L_RETPREP, 0x3C00, 0, 0, "c64r.txt");
    opclose(L_RETPREP, h);
    cb_ret = 0;
    op(L_RET0, 0x4100, 0, 0, "c64r.txt");
    dobs("", "ret 0: file still there", exists("c64r.txt"));
    h = op(L_RETPREP, 0x3C00, 0, 0, "c64r.txt");
    opclose(L_RETPREP, h);
    cb_ret = 1;
    op(L_RET1, 0x4100, 0, 0, "c64r.txt");
    dobs("", "ret 1: file still there", exists("c64r.txt"));
    cb_ret = 0x5A5A;

    /* the callback works on files itself */
    step("nest");
    nest_mode = 1;
    h = op(L_NEST, 0x3C00, 0, 0, "c64q.txt");
    nest_mode = 0;
    opclose(L_NESTDEL, h);
    op(L_NESTDEL, 0x4100, 0, 0, "c64q.txt");

    /* the same task, a second callback */
    step("again");
    dobsd("", "FileCdr(cb2), same task", FileCdr(cb2));
    dobs("", "FileCdr(-1) is cb2", FileCdr((FARPROC)-1L) == (DWORD)cb2);
    h = op(L_CB2, 0x3C00, 0, 0, "c64z.txt");
    opclose(L_CB2, h);
    op(L_CB2DEL, 0x4100, 0, 0, "c64z.txt");
    dobs("", "cb2 calls", cb2_count);
    dobsd("", "FileCdr(cb) again", FileCdr(cb));

    /* another task while we hold it */
    step("helper R");
    run_helper("R", L_HR);
    dobs("helper R: ", "started", !res[L_HR].cf);
    dobsd("helper R: ", "FileCdr(own)", sh.r1);
    dobs("helper R: ", "FileCdr(-1) is cb", sh.r2 == cbv);
    dobsd("helper R: ", "FileCdr(NULL)", sh.r3);
    dobs("helper R: ", "then FileCdr(-1)", who());

    /* removed by its owner */
    step("unreg");
    dobsd("", "FileCdr(NULL) by owner", FileCdr(NULL));
    dobs("", "FileCdr(-1) after removal", who());
    dobsd("", "FileCdr(NULL), none", FileCdr(NULL));

    /* another task installs its own callback and ends after InitApp:
       USER removes it */
    if (have_user)
        ended('Q', L_HQ, L_AQ);

}

int __cdecl tmain(void)
{
    WORD i, n;
    cb = (FARPROC)cb_entry;
    cbv = (DWORD)cb;
    out("TK64 start\r\n");
    self_task = (WORD)GetCurrentTask();
    have_user = GetModuleHandle("USER") != 0;
    /* TK64M.EXE, TK64O.EXE, TK64P.EXE (copies of TK64.EXE): only that
       case of "helper", each in a Windows of its own (the callbacks they
       leave cannot be removed) */
    n = GetModuleFileName((HINSTANCE)(entry_regs[E_DS] & ~1u), helper, sizeof helper);
    if (n > 5 && helper[n - 5] >= 'A' && helper[n - 5] <= 'Z')
        scenario = helper[n - 5];
    dobsd("", "FileCdr(-1), none", FileCdr((FARPROC)-1L));
    /* C:\TK64D, the current directory on C: */
    setr(0x0E02, 0, 0, NULL);
    R[3] = 2;
    dos_int21(R);
    setr(0x3900, 0, 0, "C:\\TK64D");
    dos_int21(R);
    setr(0x3B00, 0, 0, "C:\\TK64D");
    dos_int21(R);

    switch (scenario) {
    case 'M':
        if (have_user)
            ended('M', L_HM, L_AM);
        else
            out("(no USER: no InitApp)\r\n");
        break;
    case 'O':
        ended('O', L_HO, L_AO);
        break;
    case 'P':
        ended('P', L_HP, L_AP);
        break;
    default:
        main_run();
        break;
    }

    /* clean up (what the callback hears now is noted only) */
    step("cleanup");
    cur_op = L_CLEANUP;
    {
        static const char *const files[] = {
            "c64d.txt", "c64f.txt", "c64g.txt", "c64h.txt", "c64l.txt",
            "c64n.txt", "c64y.txt", "c64p.txt", "c64fcb.txt", "c64e.txt",
            "c64i.txt", "c64j.txt", "c64m.txt", "c64k.txt", "c64u.txt"
        };
        for (i = 0; i < sizeof files / sizeof files[0]; i++) {
            setr(0x4100, 0, 0, files[i]);
            dos_int21(R);
        }
    }
    setr(0x3B00, 0, 0, "C:\\");
    dos_int21(R);
    setr(0x3A00, 0, 0, "C:\\TK64D");
    dos_int21(R);
    cur_op = L_IDLE;
    {
        char et[8];
        lstrcpy(et, "end: ");
        if (scenario) {
            lstrcpy(et, "end ?: ");
            et[4] = scenario;
        }
        dobs(et, "-1 before", who());
        dobsd(et, "FileCdr(NULL)", FileCdr(NULL));
        dobs(et, "-1", who());
    }

    /* the results */
    step("results");
    flush();
    for (i = 1; i < L_COUNT; i++)
        report(i);
    report(L_IDLE);
    obs_env("events lost", lost);
    out("TK64 end\r\n");
    return failures;
}
