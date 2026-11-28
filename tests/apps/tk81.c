/*
 * tk81.c - KERNEL's default resource handler given a block that already
 * holds data. Found with Win32s's Freecell on 3.1 with w16's KRNL386:
 * Win32s's handler for a Win32 program's icons fills an AllocResource
 * block itself and then calls the handler it replaced (USER's, which
 * calls KERNEL's default one) with that block; w16's default handler
 * read the resource's bytes from the module's file over it (Win32s's own
 * WIN32S.EXE's), and GDI was handed a bitmap whose bits pointer lay far
 * past the block (ERR_BAD_PTR through K327). Run as the shell on 3.1
 * (Microsoft's KERNEL) and with w16's KERNEL, the logs compared (the
 * boot-test case rsrchandler):
 *   defres: the default handler (SetResourceHandler's previous one for
 *        the probe's own RCDATA, tk81.rc; NULL is refused as a handler on
 *        3.1, so a procedure of the probe's is put in and taken out
 *        again) called with AllocResource's block filled with A5h: the
 *        handle it returns and whether the contents were kept; then with
 *        that block discarded, and with 0
 */
#include <windows.h>

#define TK_REF "tk81ref.h"
#include "tklog.h"

typedef HGLOBAL (FAR PASCAL *RSRCPROC)(HGLOBAL, HINSTANCE, HRSRC);

/* never called: what SetResourceHandler is given for a moment */
static HGLOBAL FAR PASCAL dummy(HGLOBAL h, HINSTANCE i, HRSRC r)
{
    (void)i;
    (void)r;
    return h;
}

static void defres(void)
{
    HINSTANCE u = (HINSTANCE)init_regs[5];  /* DI at the start: hInstance */
    HRSRC r = FindResource(u, MAKEINTRESOURCE(1), RT_RCDATA);
    RSRCPROC def;
    HGLOBAL h, g;
    BYTE FAR *p;
    WORD i, n;
    out("defres\r\n");
    obs("defres: found", r != 0);
    if (!r)
        return;
    def = (RSRCPROC)SetResourceHandler(u, RT_RCDATA, (FARPROC)dummy);
    SetResourceHandler(u, RT_RCDATA, (FARPROC)def);
    obs("defres: handler", def != NULL);
    if (!def)
        return;
    n = (WORD)SizeofResource(u, r);
    h = AllocResource(u, r, 0);
    obs("defres: alloc size is the resource's", h && GlobalSize(h) >= n);
    if (!h)
        return;
    p = GlobalLock(h);
    for (i = 0; i < n; i++)
        p[i] = 0xA5;
    GlobalUnlock(h);
    g = def(h, u, r);
    obs("defres: full: same handle", g == h);
    obs("defres: full: other handle", g && g != h);
    if (g) {
        p = GlobalLock(g);
        obs("defres: full: first word", *(WORD FAR *)p);
        GlobalUnlock(g);
    }
    GlobalDiscard(h);
    g = def(h, u, r);
    obs("defres: discarded: same handle", g == h);
    obs("defres: discarded: other handle", g && g != h);
    if (g) {
        p = GlobalLock(g);
        obs("defres: discarded: first word", *(WORD FAR *)p);
        GlobalUnlock(g);
        if (g != h)
            GlobalFree(g);
    }
    obs("defres: discarded: block still discarded", (GlobalFlags(h) & GMEM_DISCARDED) != 0);
    GlobalFree(h);
    g = def(0, u, r);
    obs("defres: null: a block", g != 0);
    if (g) {
        p = GlobalLock(g);
        obs("defres: null: first word", *(WORD FAR *)p);
        GlobalUnlock(g);
        GlobalFree(g);
    }
}

int __cdecl tmain(void)
{
    out("TK81 start\r\n");
    defres();
    out("TK81 end\r\n");
    return failures;
}
