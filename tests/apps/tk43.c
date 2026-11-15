/*
 * tk43.c - what KERNEL's global-heap exports leave in BX, CX and ES
 * (TK31 found GlobalLock, GlobalSize, GlobalFlags, GlobalReAlloc,
 * GlobalFree, LockSegment and GlobalCompact do not keep CX or ES, and
 * GDI fails to start on w16 when GlobalLock keeps CX). Calls each on a
 * moveable and a fixed block of known size (tk31s.asm, BX = 2222h,
 * CX = 3333h, ES = DS before) and logs the registers relative to the
 * block's selector, its limit and its size.
 * Run as the shell (tests/dropin.sh, and -r for 3.1).
 */
#include <windows.h>

#define TK_REF "tk43ref.h"
#include "tklog.h"

extern WORD __cdecl w_regs[10];
extern void __cdecl __far w_call(FARPROC fn, int nargs, WORD a0, WORD a1,
                                  WORD a2, WORD a3, WORD a4, WORD a5);

static WORD sel, lim, size_lo, size_hi;

static void reg(const char *what, const char *r, WORD v)
{
    char n[48];
    wsprintf(n, "%s %s", (LPSTR)what, (LPSTR)r);
    if (v == 0x2222 || v == 0x3333)
        obs(n, 0xAAAA);                 /* kept */
    else if (v == sel)
        obs(n, 0xA001);                 /* the block's selector */
    else if (v == (sel & ~1))
        obs(n, 0xA002);                 /* its handle (selector - 1) */
    else if (v == lim)
        obs(n, 0xA003);                 /* its limit */
    else if (v == size_lo)
        obs(n, 0xA004);
    else if (v == SELECTOROF((void FAR *)&sel))
        obs(n, 0xA005);                 /* our DS */
    else
        obs(n, v);
}

static void call(const char *what, WORD ord, int n, WORD a0, WORD a1, WORD a2, WORD a3)
{
    FARPROC f = GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(ord));
    w_call(f, n, a0, a1, a2, a3, 0, 0);
    reg(what, "AX", w_regs[0]);
    reg(what, "BX", w_regs[1]);
    reg(what, "CX", w_regs[2]);
    reg(what, "DX", w_regs[3]);
    reg(what, "ES", w_regs[8]);
}

static void block(const char *kind, WORD flags)
{
    HGLOBAL h = GlobalAlloc(flags, 1000);
    DWORD s = GlobalSize(h);
    sel = SELECTOROF(GlobalLock(h));
    GlobalUnlock(h);
    lim = (WORD)GetSelectorLimit(sel);
    size_lo = LOWORD(s);
    size_hi = HIWORD(s);
    out(kind);
    out("\r\n");
    obs("  handle is selector - 1", (WORD)h == (sel & ~1) && (WORD)h != sel);
    obs("  limit", lim);
    obs("  size", size_lo);
    call("  GlobalLock", 18, 1, (WORD)h, 0, 0, 0);
    call("  GlobalUnlock", 19, 1, (WORD)h, 0, 0, 0);
    call("  GlobalSize", 20, 1, (WORD)h, 0, 0, 0);
    call("  GlobalFlags", 22, 1, (WORD)h, 0, 0, 0);
    call("  GlobalHandle", 21, 1, sel, 0, 0, 0);
    call("  GlobalReAlloc", 16, 4, (WORD)h, 0, 2000, GMEM_MOVEABLE);
    s = GlobalSize(h);
    lim = (WORD)GetSelectorLimit(sel);
    size_lo = LOWORD(s);
    call("  GlobalFree", 17, 1, (WORD)h, 0, 0, 0);
}

int __cdecl tmain(void)
{
    out("TK43 start\r\n");
    block("moveable", GMEM_MOVEABLE);
    block("fixed", GMEM_FIXED);
    block("discardable", GMEM_MOVEABLE | GMEM_DISCARDABLE);
    {
        /* the local heap (our DS); sel and lim are set so they match
           nothing, size_lo is the size asked */
        WORD i, h;
        static const WORD fl[3] = { LMEM_FIXED, LMEM_MOVEABLE, LMEM_FIXED | LMEM_ZEROINIT };
        char n[32];
        for (i = 0; i < 3; i++) {
            wsprintf(n, "LocalAlloc(%04X, 100)", fl[i]);
            out(n);
            out("\r\n");
            sel = 0x0001;
            lim = 0x0002;
            size_lo = 100;
            call("  LocalAlloc", 5, 2, fl[i], 100, 0, 0);
            h = w_regs[0];
            obs("  CX - AX", w_regs[2] - w_regs[0]);
            obs("  BX - AX", w_regs[1] - w_regs[0]);
            call("  LocalLock", 8, 1, h, 0, 0, 0);
            obs("  LocalLock CX - AX", w_regs[2] - w_regs[0]);
            call("  LocalSize", 10, 1, h, 0, 0, 0);
            call("  LocalFlags", 12, 1, h, 0, 0, 0);
            call("  LocalUnlock", 9, 1, h, 0, 0, 0);
            call("  LocalReAlloc", 6, 3, h, 200, LMEM_MOVEABLE, 0);
            h = w_regs[0];
            call("  LocalFree", 7, 1, h, 0, 0, 0);
        }
    }
    out("TK43 done\r\n");
    return 0;
}
