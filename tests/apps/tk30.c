/*
 * tk30.c - icons of DLLs (Control Panel's applets showed no icons on
 * w16): LoadIcon of MAIN.CPL's first icon, the block it gives, and
 * whether it draws. Run as the shell (tests/dropin.sh, and -r for 3.1).
 */
#include <windows.h>

#define TK_REF "tk30ref.h"
#include "tklog.h"

static WORD drawn(HICON h)
{
    HDC sdc = GetDC(0), dc = CreateCompatibleDC(sdc);
    HBITMAP bm = CreateCompatibleBitmap(sdc, 32, 32), old;
    WORD x, y, n = 0;
    RECT r;
    old = SelectObject(dc, bm);
    SetRect(&r, 0, 0, 32, 32);
    FillRect(dc, &r, GetStockObject(WHITE_BRUSH));
    DrawIcon(dc, 0, 0, h);
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++)
            if (GetPixel(dc, x, y) != RGB(255, 255, 255))
                n++;
    SelectObject(dc, old);
    DeleteObject(bm);
    DeleteDC(dc);
    ReleaseDC(0, sdc);
    return n;
}

int __cdecl tmain(void)
{
    HINSTANCE h;
    HICON ic;
    out("TK30 start\r\n");
    h = LoadLibrary("MAIN.CPL");
    obs("MAIN.CPL loaded", h >= 32);
    if (h >= 32) {
        WORD id;
        for (id = 1; id < 400; id++)
            if (FindResource(h, MAKEINTRESOURCE(id), RT_GROUP_ICON))
                break;
        obs("MAIN.CPL first group icon id", id);
        ic = LoadIcon(h, MAKEINTRESOURCE(id));
        obs("MAIN.CPL LoadIcon != 0", ic != 0);
        if (ic) {
            WORD FAR *p;
            obs("MAIN.CPL icon GlobalFlags before lock", GlobalFlags(ic));
            obs("MAIN.CPL icon GlobalHandle != 0", GlobalHandle(ic) != 0);
            p = (WORD FAR *)GlobalLock(ic);
            obs("MAIN.CPL icon pixels drawn", drawn(ic));
            obs("MAIN.CPL icon GlobalSize", (WORD)GlobalSize(ic));
            obs("MAIN.CPL icon GlobalLock != 0", p != 0);
            obs("MAIN.CPL icon GlobalFlags", GlobalFlags(ic));
            obs("MAIN.CPL icon handle odd", ic & 1);
            if (p)
                dump("MAIN.CPL icon", SELECTOROF(p), OFFSETOF(p), 6);
            GlobalUnlock(ic);
        }
        FreeLibrary(h);
    }
    {
        /* a discarded block brought back by GlobalReAlloc */
        static const WORD fl[3] = { GMEM_MOVEABLE, 0, GMEM_MOVEABLE | GMEM_DISCARDABLE };
        WORD i;
        for (i = 0; i < 3; i++) {
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE, 0), g2;
            char n[48];
            obsn("discarded block GlobalFlags, realloc flags", fl[i], GlobalFlags(g));
            g2 = GlobalReAlloc(g, 0x100, fl[i]);
            obsn("realloc: same handle, flags", fl[i], g2 == g);
            obsn("realloc: != 0, flags", fl[i], g2 != 0);
            if (g2) {
                obsn("realloc: GlobalFlags, flags", fl[i], GlobalFlags(g2));
                obsn("realloc: GlobalSize, flags", fl[i], (WORD)GlobalSize(g2));
                GlobalFree(g2);
            }
            (void)n;
        }
    }
    out("TK30 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
