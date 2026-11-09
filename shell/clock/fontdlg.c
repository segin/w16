/*
 * fontdlg.c - Settings > Set Font... (digital clock).
 *
 * Observed on Windows 3.1: the original calls ChooseFont with its own
 * template (resource 100: face list, OK, Cancel, sample; the style and
 * size controls are placed off the dialog), offering the screen fonts.
 * It starts from the font it last made (the size list shows its height)
 * and keeps the whole LOGFONT ChooseFont returns (face, family, pitch and
 * precision bits all reach its CreateFontIndirect calls), then writes
 * sFont=<face> to CLOCK.INI at once.

 */
#include "clock.h"
#include <commdlg.h>

BOOL font_choose(HWND owner)
{
    CHOOSEFONT cf;
    LOGFONT lf = g_lf;
    int i;
    char FAR *p = (char FAR *)&cf;
    for (i = 0; i < (int)sizeof cf; i++)
        p[i] = 0;
    cf.lStructSize = sizeof cf;
    cf.hwndOwner = owner;
    cf.lpLogFont = &lf;
    /* the faces offered: no OEM or symbol character sets (observed: no
       Terminal, Modern, Roman, Script, Symbol, Wingdings) */
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_ENABLETEMPLATE | CF_ANSIONLY;
    cf.hInstance = g_hinst;
    cf.lpTemplateName = MAKEINTRESOURCE(IDD_FONT);
    if (!ChooseFont(&cf))
        return FALSE;
    g_lf = lf;
    return TRUE;
}

