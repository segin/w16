/*
 * msgbox.c - MessageBox.
 *
 * UNVERIFIED: not yet the dialog Windows 3.1 shows; the text goes to w16's
 * USER log and the default button's answer is returned.
 */
#include "user.h"

int WINAPI MessageBox(HWND h, LPCSTR text, LPCSTR caption, UINT type)
{
    (void)h;
    ulog("MessageBox: ");
    if (caption)
        ulog(caption);
    ulog(": ");
    if (text)
        ulog(text);
    ulog("\r\n");
    switch (type & MB_TYPEMASK) {
    case MB_OKCANCEL:
        return (type & MB_DEFBUTTON2) ? IDCANCEL : IDOK;
    case MB_ABORTRETRYIGNORE:
        return IDABORT;
    case MB_YESNOCANCEL:
    case MB_YESNO:
        return (type & MB_DEFBUTTON2) ? IDNO : IDYES;
    case MB_RETRYCANCEL:
        return IDRETRY;
    }
    return IDOK;
}
