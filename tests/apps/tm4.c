/*
 * tm4.c - probe TM4: the window rectangle CreateDialogIndirect gives
 * top-level dialogs of a few frame styles (ARCHITECTURE.md 15.5: Windows
 * 3.1's Calculator is such a dialog, template style 00CA0000h at 310,65,
 * 240 x 158 units, no font; Character Map's, 84CA0040h with 8 point
 * Helv at 9,33, 348 x 118). Run as the shell; logs to TK.LOG, per style
 * and place: the window and client rectangles as made.
 */
#include <windows.h>

#define TK_REF "tm3ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

BOOL FAR PASCAL __export __loadds tm4_dlg(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    (void)h, (void)m, (void)wp, (void)lp;
    return FALSE;
}

/* a DLGTEMPLATE with no items, menu, class or font */
static BYTE tpl[48];

static void one(HINSTANCE hi, DWORD style, int x, int y, int cx, int cy)
{
    HWND h;
    RECT r, c;
    char b[120];
    BYTE *p = tpl;
    *(DWORD *)p = style; p += 4;
    *p++ = 0;                                   /* items */
    *(int *)p = x; p += 2;
    *(int *)p = y; p += 2;
    *(int *)p = cx; p += 2;
    *(int *)p = cy; p += 2;
    *p++ = 0;                                   /* menu */
    *p++ = 0;                                   /* class */
    *p++ = 'T'; *p++ = 0;                       /* caption */
    if (style & DS_SETFONT) {                   /* 8 point Helv */
        *(int *)p = 8; p += 2;
        lstrcpy((LPSTR)p, "Helv"); p += 5;
    }
    h = CreateDialogIndirect(hi, (LPCSTR)tpl, 0, (DLGPROC)tm4_dlg);
    if (!h) {
        wsprintf(b, "style %08lX at %d,%d %dx%d: none\r\n", style, x, y, cx, cy);
        out(b);
        return;
    }
    GetWindowRect(h, &r);
    GetClientRect(h, &c);
    wsprintf(b, "style %08lX at %d,%d %dx%d: window %d,%d %dx%d client %dx%d\r\n", style, x, y, cx, cy,
             r.left, r.top, r.right - r.left, r.bottom - r.top, c.right, c.bottom);
    out(b);
    DestroyWindow(h);
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    static const DWORD st[] = { 0x00CA0000L, 0x00C80000L, 0x00800000L, 0x00400000L,
                                0x80C80080L, 0x00CA0080L, 0x80800000L, 0 };
    int i;
    WaitEvent(0);
    InitApp(hi);
    out("TM4 start\r\n");
    for (i = 0; st[i]; i++) {
        one(hi, st[i], 10, 10, 100, 50);
        one(hi, st[i], 310, 65, 240, 158);
    }
    /* Character Map's template: 84CA0040h, 8 point Helv */
    one(hi, 0x84CA0040L, 9, 33, 348, 118);
    one(hi, 0x84CA00C0L, 9, 33, 348, 118);
    out("done\r\n");
    return 0;
}
