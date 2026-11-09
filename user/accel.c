/*
 * accel.c - keyboard accelerators (3.1 SDK): an RT_ACCELERATOR resource is
 * a table of 5-byte entries (flags, key, command); the last entry's flags
 * have 80h set. Flags: 01h virtual key, 02h no top-level menu flash,
 * 04h Shift, 08h Ctrl, 10h Alt.
 */
#include "user.h"

HACCEL WINAPI LoadAccelerators(HINSTANCE hinst, LPCSTR name)
{
    HRSRC r = FindResource(hinst, name, RT_ACCELERATOR);
    return r ? (HACCEL)LoadResource(hinst, r) : 0;
}

int WINAPI TranslateAccelerator(HWND h, HACCEL ha, MSG __far *m)
{
    const BYTE __far *p;
    BOOL ctrl, shift, alt;
    UINT key;
    BOOL virt;
    int found = 0;
    if (!ha || !valid_wnd(h))
        return 0;
    switch (m->message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        virt = TRUE;
        break;
    case WM_CHAR:
    case WM_SYSCHAR:
        virt = FALSE;
        break;
    default:
        return 0;
    }
    key = m->wParam;
    ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    alt = (m->lParam & 0x20000000L) != 0;
    p = (const BYTE __far *)LockResource((HGLOBAL)ha);
    if (!p)
        return 0;
    for (;; p += 5) {
        BYTE f = p[0];
        UINT k = *(const WORD __far *)(p + 1);
        UINT cmd = *(const WORD __far *)(p + 3);
        if (((f & 1) != 0) == virt) {
            BOOL match;
            if (virt)
                match = k == key && ((f & 4) != 0) == shift && ((f & 8) != 0) == ctrl &&
                        ((f & 0x10) != 0) == alt;
            else
                match = k == key && ((f & 0x10) != 0) == alt;
            if (match) {
                HMENU menu;
                UINT state;
                menu = GetMenu(h);
                state = menu ? GetMenuState(menu, cmd, MF_BYCOMMAND) : (UINT)-1;
                if (state == (UINT)-1 || !(state & (MF_GRAYED | MF_DISABLED))) {
                    if (menu)
                        send_message(h, WM_INITMENU, (WPARAM)menu, 0);
                    send_message(h, (m->message == WM_SYSKEYDOWN || m->message == WM_SYSCHAR) &&
                                 cmd >= 0xF000 ? WM_SYSCOMMAND : WM_COMMAND, cmd, MAKELONG(0, 1));
                }
                found = 1;
                break;
            }
        }
        if (f & 0x80)
            break;
    }
    GlobalUnlock((HGLOBAL)ha);
    return found;
}
