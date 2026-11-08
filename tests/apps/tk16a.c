/*
 * tk16a.c - the helper tasks of TK16 (tests/apps/tk16.c), one source:
 *   TK16A.EXE  plain
 *   TK16B.EXE  (TK16_BAD) imports TKBAD.DLL, whose initialisation fails
 *   TK16C.EXE  (TK16_FAR) has a second writable data segment
 * They report into TK16's DGROUP (tk16.h); start-up code tk15s.asm.
 */
#include <windows.h>
#define TK15_HELPER
#include "tk15.h"
#include "tk16.h"

extern WORD __cdecl entry_regs[11];   /* ax bx cx dx si di bp sp ds es ss */
extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */
extern int __cdecl dos_int21(WORD FAR *r);

#ifdef TK16_BAD
extern WORD FAR PASCAL ID_GET(void);            /* TKBAD.DLL */
#endif
#ifdef TK16_FAR
static WORD __far fardata[64] = { 1 };
#endif

static WORD seg_limit(WORD sel);
#pragma aux seg_limit = ".286p" "lsl ax, dx" parm [dx] value [ax];

static tk16sh FAR *sh;
static WORD FAR *v;
static WORD ptask;

static void args(const char FAR *p, char *mode)
{
    WORD slot, ds, off;
    while (*p == ' ')
        p++;
    *mode = *p++;
    slot = hexw(&p);
    ds = hexw(&p);
    off = hexw(&p);
    ptask = hexw(&p);
    sh = slot < TK16_SLOTS ? MAKELP(ds, off) : NULL;
    v = sh ? sh->v[slot] : NULL;
}

/* before InitTask: the PSP's tail (ES at entry) gives the slot */
void __cdecl pre_init(void)
{
    BYTE FAR *psp = MAKELP(entry_regs[9], 0);
    char mode, buf[0x80];
    WORD i, n = psp[0x80];
    for (i = 0; i < n && i < sizeof buf - 1; i++)
        buf[i] = psp[0x81 + i];
    buf[i] = 0;
    args(buf, &mode);
    if (v)
        v[W_PRE] = 1;
}

int __cdecl tmain(void)
{
    BYTE FAR *psp = MAKELP(init_regs[6], 0);
    const char FAR *env, FAR *penv;
    char mode, cwd[70];
    WORD i, r[8], n, lim;

    args(MAKELP(init_regs[6], init_regs[1]), &mode);
    if (!v)
        return 1;
    v[W_RAN] = 1;
    v[W_HPREV] = init_regs[4];
    /* the tail as it was: InitTask put a NUL where the CR was */
    v[W_TAILLEN] = psp[0x80];
    for (i = 0; i < 24; i++)
        ((BYTE FAR *)&v[W_TAIL])[i] = psp[0x81 + i];
    for (i = 0; i < 8; i++)
        r[i] = 0;
    r[0] = 0x4700;
    r[4] = (WORD)(DWORD)(char FAR *)cwd;
    r[6] = (WORD)((DWORD)(char FAR *)cwd >> 16);
    cwd[0] = 0;
    dos_int21((WORD FAR *)r);
    for (i = 0; i < 23 && cwd[i]; i++)
        ((char FAR *)&v[W_CWD])[i] = cwd[i];
    env = MAKELP(*(WORD FAR *)(psp + 0x2C), 0);
    penv = MAKELP(tdbw(tdbw(ptask, 0x60), 0x2C), 0);
    v[W_ENVSAME] = env == penv;
    v[W_ENVSEL] = *(WORD FAR *)(psp + 0x2C);
    for (n = 0; n < 4000 && (env[n] || env[n + 1]); n++)
        ;
    v[W_ENVLEN] = n;
    lim = seg_limit(v[W_ENVSEL]);
    v[W_ENVTAIL] = n + 3 <= lim ? *(WORD FAR *)(env + n + 2) : 0xFFFF;
    for (i = 0; i < 8; i++)
        if (n + 4 + i <= lim)
            ((char FAR *)&v[W_ENVPATH])[i] = env[n + 4 + i];
    for (i = 0; i < 16 && env[i]; i++)
        ((char FAR *)&v[W_ENV])[i] = env[i];
#ifdef TK16_BAD
    (void)ID_GET;
#endif
#ifdef TK16_FAR
    fardata[1] = fardata[0];
#endif
    if (mode == 'W')
        for (n = 0; n < 400 && !sh->go; n++)
            K_YIELD();
    return 0;
}
