/*
 * class.c - window classes.
 *
 * A class lives in USER's local heap (CLS, then cbClsExtra bytes). Its name
 * is an atom of USER's local atom table, so GetClassInfo's return value is
 * that atom (Windows 3.1 returns C302h-style values for the system classes
 * and 8000h-8004h for the integer-atom classes #32768-#32772: probe US2).
 * Application classes are found by name and hInstance (module), system
 * classes (hInstance 0 here) by name alone (3.1 SDK, RegisterClass).
 */
#include "user.h"

static WORD classes;            /* near list */

static ATOM class_atom(LPCSTR name, BOOL add)
{
    if (ISATOM(name))
        return LOWORD((DWORD)name);
    if (name[0] == '#') {               /* "#32770" means atom 32770 */
        WORD v = 0;
        LPCSTR p = name + 1;
        while (*p >= '0' && *p <= '9')
            v = v * 10 + (*p++ - '0');
        if (!*p && v)
            return v;
    }
    return add ? AddAtom(name) : FindAtom(name);
}

static HMODULE module_of(HINSTANCE hinst)
{
    return hinst ? K_GETEXEPTR(hinst) : 0;
}

/* integer names: an atom value or "#nnnnn"; 0 for a string name */
static WORD int_name(LPCSTR name)
{
    if (ISATOM(name))
        return LOWORD((DWORD)name);
    if (name[0] == '#') {
        WORD v = 0;
        LPCSTR p = name + 1;
        while (*p >= '0' && *p <= '9')
            v = v * 10 + (*p++ - '0');
        if (!*p && v)
            return v;
    }
    return 0;
}

static BOOL same_name(PCLS c, LPCSTR name)
{
    WORD v = int_name(name);
    if (v)
        return c->atom == v && (!c->name || v >= 0xC000u);
    return c->name && !lstr_cmpi((LPCSTR)MAKELP(get_ds(), c->name), name);
}

/* classes are found by name, compared without regard to case, as atoms
   are; an application's own (its module's) before the system's */
PCLS find_class(LPCSTR name, HINSTANCE hinst)
{
    HMODULE m = module_of(hinst);
    PCLS c, sys = 0;
    for (c = (PCLS)classes; c; c = (PCLS)c->next) {
        if (!same_name(c, name))
            continue;
        if (c->hModule == m && m)
            return c;
        if (c->style & CS_GLOBALCLASS)
            sys = c;
        if (!c->hInstance)
            sys = sys ? sys : c;
    }
    return sys;
}

static PCLS register_class(const WNDCLASS __far *wc, BOOL system)
{
    PCLS c;
    HMODULE m = system ? 0 : module_of(wc->hInstance);
    ATOM a;
    for (c = (PCLS)classes; c; c = (PCLS)c->next)
        if (same_name(c, wc->lpszClassName) && c->hModule == m && (!system || !c->hInstance))
            return 0;                   /* already registered by this module */
    a = class_atom(wc->lpszClassName, TRUE);
    if (!a)
        return 0;
    c = (PCLS)LocalAlloc(LPTR, sizeof(CLS) + (wc->cbClsExtra > 0 ? wc->cbClsExtra : 0));
    if (!c)
        return 0;
    c->atom = a;
    if (!int_name(wc->lpszClassName))
        c->name = local_strdup(wc->lpszClassName);
    c->style = wc->style;
    c->lpfnWndProc = wc->lpfnWndProc;
    c->cbClsExtra = wc->cbClsExtra > 0 ? wc->cbClsExtra : 0;
    c->cbWndExtra = wc->cbWndExtra > 0 ? wc->cbWndExtra : 0;
    c->hInstance = system ? 0 : wc->hInstance;
    c->hModule = m;
    c->hIcon = wc->hIcon;
    c->hCursor = wc->hCursor;
    c->hbrBackground = wc->hbrBackground;
    c->lpszMenuName = wc->lpszMenuName;
    if (wc->lpszMenuName && !ISATOM(wc->lpszMenuName)) {
        c->menuname = local_strdup(wc->lpszMenuName);
        c->lpszMenuName = (LPCSTR)MAKELP(get_ds(), c->menuname);
    }
    c->next = classes;
    classes = (WORD)c;
    return c;
}

ATOM WINAPI RegisterClass(const WNDCLASS __far *wc)
{
    PCLS c;
    c = register_class(wc, FALSE);
    return c ? c->atom : 0;
}

BOOL WINAPI UnregisterClass(LPCSTR name, HINSTANCE hinst)
{
    PCLS c = find_class(name, hinst);
    WORD *link;
    if (!c || !c->hInstance || c->cwnd)
        return FALSE;
    for (link = &classes; *link; link = &((PCLS)*link)->next)
        if (*link == (WORD)c) {
            *link = c->next;
            break;
        }
    if (c->menuname)
        LocalFree((HLOCAL)c->menuname);
    if (c->name)
        LocalFree((HLOCAL)c->name);
    if (c->atom >= 0xC000u)
        DeleteAtom(c->atom);
    LocalFree((HLOCAL)c);
    return TRUE;
}

BOOL WINAPI GetClassInfo(HINSTANCE hinst, LPCSTR name, WNDCLASS __far *wc)
{
    PCLS c = find_class(name, hinst);
    if (!c)
        return FALSE;
    wc->style = c->style;
    wc->lpfnWndProc = c->lpfnWndProc;
    wc->cbClsExtra = c->cbClsExtra;
    wc->cbWndExtra = c->cbWndExtra;
    wc->hInstance = c->hInstance;
    wc->hIcon = c->hIcon;
    wc->hCursor = c->hCursor;
    wc->hbrBackground = c->hbrBackground;
    wc->lpszMenuName = c->lpszMenuName;
    wc->lpszClassName = name;
    return c->atom;
}

int WINAPI GetClassName(HWND h, LPSTR buf, int n)
{
    PWND w = valid_wnd(h);
    PCLS c;
    if (!w || n <= 0)
        return 0;
    c = (PCLS)w->cls;
    if (c->atom < 0xC000u) {
        /* an integer atom: "#nnnnn" */
        char t[8];
        WORD v = c->atom, i = 7;
        t[i] = 0;
        do {
            t[--i] = (char)('0' + v % 10);
            v /= 10;
        } while (v);
        t[--i] = '#';
        lstr_cpyn(buf, t + i, n);
        return lstr_len(buf);
    }
    return GetAtomName(c->atom, buf, n);
}

static void __far *class_field(PCLS c, int i, WORD size)
{
    if (i >= 0 && i + size <= (WORD)c->cbClsExtra)
        return (BYTE __far *)(c + 1) + i;
    return 0;
}

WORD WINAPI GetClassWord(HWND h, int i)
{
    PWND w = valid_wnd(h);
    PCLS c;
    WORD __far *p;
    if (!w)
        return 0;
    c = (PCLS)w->cls;
    switch (i) {
    case GCW_HBRBACKGROUND: return (WORD)c->hbrBackground;
    case GCW_HCURSOR: return (WORD)c->hCursor;
    case GCW_HICON: return (WORD)c->hIcon;
    case GCW_HMODULE: return (WORD)c->hModule;
    case GCW_CBWNDEXTRA: return c->cbWndExtra;
    case GCW_CBCLSEXTRA: return c->cbClsExtra;
    case GCW_STYLE: return c->style;
    case -32: return c->atom;           /* GCW_ATOM (3.1 SDK) */
    }
    p = class_field(c, i, 2);
    return p ? *p : 0;
}

WORD WINAPI SetClassWord(HWND h, int i, WORD v)
{
    PWND w = valid_wnd(h);
    PCLS c;
    WORD old, __far *p;
    if (!w)
        return 0;
    c = (PCLS)w->cls;
    switch (i) {
    case GCW_HBRBACKGROUND: old = (WORD)c->hbrBackground; c->hbrBackground = (HBRUSH)v; return old;
    case GCW_HCURSOR: old = (WORD)c->hCursor; c->hCursor = (HCURSOR)v; return old;
    case GCW_HICON: old = (WORD)c->hIcon; c->hIcon = (HICON)v; return old;
    case GCW_STYLE: old = c->style; c->style = v; return old;
    }
    p = class_field(c, i, 2);
    if (!p)
        return 0;
    old = *p;
    *p = v;
    return old;
}

LONG WINAPI GetClassLong(HWND h, int i)
{
    PWND w = valid_wnd(h);
    PCLS c;
    LONG __far *p;
    if (!w)
        return 0;
    c = (PCLS)w->cls;
    if (i == GCL_WNDPROC)
        return (LONG)c->lpfnWndProc;
    if (i == GCL_MENUNAME)
        return (LONG)c->lpszMenuName;
    p = class_field(c, i, 4);
    return p ? *p : 0;
}

LONG WINAPI SetClassLong(HWND h, int i, LONG v)
{
    PWND w = valid_wnd(h);
    PCLS c;
    LONG old, __far *p;
    if (!w)
        return 0;
    c = (PCLS)w->cls;
    if (i == GCL_WNDPROC) {
        old = (LONG)c->lpfnWndProc;
        c->lpfnWndProc = (WNDPROC)v;
        return old;
    }
    p = class_field(c, i, 4);
    if (!p)
        return 0;
    old = *p;
    *p = v;
    return old;
}

/* ---- the system classes ---- */
extern LONG WINAPI DefWindowProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI DesktopWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI MenuWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI IconTitleWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI MDIClientWndProc(HWND, UINT, WPARAM, LPARAM);

static void sysclass(LPCSTR name, UINT style, WNDPROC proc, int wndextra, HBRUSH br, LPCSTR cursor)
{
    WNDCLASS wc;
    wc.style = style;
    wc.lpfnWndProc = proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = wndextra;
    wc.hInstance = hInstUser;
    wc.hIcon = 0;
    wc.hCursor = cursor ? load_icon_cursor(0, cursor, TRUE) : 0;
    wc.hbrBackground = br;
    wc.lpszMenuName = 0;
    wc.lpszClassName = name;
    register_class(&wc, TRUE);
}

void register_system_classes(void)
{
    /* styles, extra bytes and brushes as Windows 3.1 reports them (US2) */
    sysclass(MAKEINTRESOURCE(32769), CS_DBLCLKS, (WNDPROC)DesktopWndProc, 0,
             (HBRUSH)(COLOR_BACKGROUND + 1), IDC_ARROW);
    sysclass(MAKEINTRESOURCE(32772), 0, (WNDPROC)IconTitleWndProc, 0, 0, IDC_ARROW);
    sysclass(MAKEINTRESOURCE(32768), CS_SAVEBITS, (WNDPROC)MenuWndProc, 2, 0, NULL);
    sysclass("MDICLIENT", 0, (WNDPROC)MDIClientWndProc, 16,
             (HBRUSH)(COLOR_APPWORKSPACE + 1), IDC_ARROW);
}
