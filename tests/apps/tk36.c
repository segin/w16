/*
 * tk36.c - the volume label through the DOS extender: INT 21h AH=4Eh
 * (find first) on "C:\*.*" with attribute 08h, and an FCB search
 * (AH=11h, extended FCB with attribute 08h), as File Manager reads the
 * label; then 11h/12h with a plain FCB and buffers above and below
 * 1 MB, and 16h/10h/0Fh/10h/13h on C:TK36.TMP. Microsoft's DOSX:
 * 11h/12h find entries (translated), 16h/0Fh/13h give AL=FFh (not
 * translated). Run as the shell with tests/dropin.sh, and -x (w16's).
 */
#include <windows.h>

#define TK_REF "tk36ref.h"
#include "tklog.h"

static BYTE dta[128];
static char spec[] = "C:\\*.*";
static BYTE xfcb[44];

static WORD setdta(void FAR *p);
#pragma aux setdta = "push ds" "mov ds, dx" "mov dx, ax" "mov ah, 1Ah" \
    "int 21h" "pop ds" parm [dx ax] value [ax];
static WORD find(const char FAR *s, WORD attr);
#pragma aux find = "push ds" "mov ds, dx" "mov dx, ax" "mov ah, 4Eh" \
    "int 21h" "sbb dx, dx" "pop ds" parm [dx ax] [cx] value [dx];
static WORD fcbcall(void FAR *f, WORD ax);
#pragma aux fcbcall = "push ds" "mov ds, dx" "mov dx, bx" \
    "int 21h" "xor ah, ah" "pop ds" parm [dx bx] [ax] value [ax];
static WORD fcbfind(void FAR *f);
#pragma aux fcbfind = "push ds" "mov ds, dx" "mov dx, ax" "mov ah, 11h" \
    "int 21h" "xor ah, ah" "pop ds" parm [dx ax] value [ax];

int __cdecl tmain(void)
{
    char name[16];
    WORD i, r;
    out("TK36 start\r\n");
    setdta(dta);
    r = find(spec, 0x08);
    obs("4Eh attr 08h carry", r & 1);
    for (i = 0; i < 13 && dta[30 + i]; i++)
        name[i] = dta[30 + i];
    name[i] = 0;
    obs_str("4Eh name", name);
    obs("4Eh attribute", dta[21]);
    xfcb[0] = 0xFF;                     /* extended FCB */
    xfcb[6] = 0x08;                     /* attribute: volume label */
    xfcb[7] = 3;                        /* drive C: */
    for (i = 0; i < 11; i++)
        xfcb[8 + i] = '?';
    for (i = 0; i < 44; i++)
        dta[i] = 0;
    r = fcbfind(xfcb);
    obs("11h AL", r);
    for (i = 0; i < 11; i++)
        name[i] = dta[8 + i] ? dta[8 + i] : '.';
    name[11] = 0;
    obs_str("11h name", name);
    obs("DGROUP base hi", (WORD)(GetSelectorBase(SELECTOROF((void FAR *)dta)) >> 16));
    {
        /* the same through buffers above and below 1 MB */
        DWORD lo = GlobalDosAlloc(128);
        HGLOBAL h = GlobalAlloc(GMEM_FIXED, 128);
        BYTE FAR *f[2];
        WORD k;
        f[0] = (BYTE FAR *)MAKELP(LOWORD(lo), 0);
        f[1] = (BYTE FAR *)GlobalLock(h);
        for (k = 0; k < 2; k++) {
            BYTE FAR *x = f[k];
            obs("buffer base hi", (WORD)(GetSelectorBase(SELECTOROF(x)) >> 16));
            for (i = 0; i < 128; i++)
                x[i] = 0;
            x[64] = 0xFF; x[70] = 0x08; x[71] = 3;
            for (i = 0; i < 11; i++)
                x[72 + i] = '?';
            setdta(x);
            r = fcbfind(x + 64);
            obs("11h AL", r);
            for (i = 0; i < 11; i++)
                name[i] = x[8 + i] ? x[8 + i] : '.';
            obs_str("11h name", name);
        }
        {
            /* 11h and 12h with a plain FCB "C:????????.???" above 1 MB */
            BYTE FAR *x = f[1];
            for (i = 0; i < 128; i++)
                x[i] = 0;
            x[64] = 3;
            for (i = 0; i < 11; i++)
                x[65 + i] = '?';
            r = fcbfind(x + 64);
            obs("plain 11h AL", r);
            for (i = 0; i < 11; i++)
                name[i] = x[1 + i] ? x[1 + i] : '.';
            obs_str("plain 11h name", name);
            r = fcbcall(x + 64, 0x1200);
            obs("plain 12h AL", r);
            for (i = 0; i < 11; i++)
                name[i] = x[1 + i] ? x[1 + i] : '.';
            obs_str("plain 12h name", name);
        }
        setdta(dta);
        {
            /* the other FCB functions on C:TK36.TMP (an FCB above 1 MB) */
            static const char nm[] = "TK36    TMP";
            static const WORD fn[] = { 0x1600, 0x1000, 0x0F00, 0x1000,
                                       0x1300 };
            BYTE FAR *x = f[1];
            for (k = 0; k < 5; k++) {
                if (k != 1 && k != 3) {     /* close: as open left it */
                    for (i = 0; i < 44; i++)
                        x[i] = 0;
                    x[0] = 3;
                    for (i = 0; i < 11; i++)
                        x[1 + i] = nm[i];
                }
                r = fcbcall(x, fn[k]);
                obs("FCB AH", fn[k] >> 8);
                obs("  AL", r);
            }
        }
    }
    out("TK36 done\r\n");
    return 0;
}
