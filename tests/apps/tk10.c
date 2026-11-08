/*
 * tk10.c - floating-point OS fixups in the record forms wlink does not
 * write: a chain (a record without the additive flag whose site holds
 * the offset of the next site, FFFFh ending it) and a lone record without
 * the additive flag. TK10.EXE is rewritten after linking by
 * tools/nechain.py (see there for the four sites A-D); this dumps the
 * sites after loading, with and without a coprocessor (tests/tk6ref.sh).
 */
#include <windows.h>

#define TK_REF "tk10ref.h"
#include "tklog.h"

extern BYTE FAR * __cdecl chn_site(WORD i);

int __cdecl tmain(void)
{
    static const char *const n87[4][4] = {
        { "87.A.w0", "87.A.w1", "87.A.w2", "87.A.w3" },
        { "87.B.w0", "87.B.w1", "87.B.w2", "87.B.w3" },
        { "87.C.w0", "87.C.w1", "87.C.w2", "87.C.w3" },
        { "87.D.w0", "87.D.w1", "87.D.w2", "87.D.w3" },
    };
    static const char *const nem[4][4] = {
        { "em.A.w0", "em.A.w1", "em.A.w2", "em.A.w3" },
        { "em.B.w0", "em.B.w1", "em.B.w2", "em.B.w3" },
        { "em.C.w0", "em.C.w1", "em.C.w2", "em.C.w3" },
        { "em.D.w0", "em.D.w1", "em.D.w2", "em.D.w3" },
    };
    int have87 = (GetWinFlags() & WF_80x87) != 0;
    WORD i, k;
    out("TK10 start\r\n");
    obs_env("WF_80x87", have87);
    obs_env("A", (WORD)(DWORD)chn_site(0));
    for (i = 0; i < 4; i++) {
        WORD FAR *w = (WORD FAR *)chn_site(i);
        for (k = 0; k < 4; k++)
            obs(have87 ? n87[i][k] : nem[i][k], w[k]);
    }
    out("TK10 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
