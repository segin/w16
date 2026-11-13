/*
 * class.c - window classes.
 *
 * A class is a CLS in USER's local heap (DGROUP), then cbClsExtra bytes,
 * with the layout of Schulman's CLASS (Undocumented Windows, ch. 6), which
 * probe US15 read on 3.1: hcNext, the 'NK' signature, the name's atom (in
 * USER's DGROUP atom table: GlobalFindAtom does not find it), hDCE, the
 * window count, then the WNDCLASS with hInstance the module and both
 * string pointers 0 (3.1's block: 24h + the extra bytes, and one atom).
 * w16 keeps a string menu name as a copy in its string heap (uheap.c),
 * lpszMenuName pointing to it. Classes are found by name (atoms compare
 * without regard to case) and module; system classes (USER's module)
 * by name alone (3.1 SDK, RegisterClass). GetClassInfo returns the
 * class's atom (Windows 3.1 returns C302h-style values for the system
 * classes and 8000h-8004h for the integer-atom classes #32768-#32772:
 * probe US2).
 */
#include "user.h"

static WORD classes;            /* near list */

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

/* the atom a name is known by: 0 if no class could have it */
static ATOM name_atom(LPCSTR name)
{
    WORD v = int_name(name);
    return v ? v : FindAtom(name);
}

#define IS_SYSTEM(c)   ((c)->hModule == hModUser)

/* classes are found by name, compared without regard to case, as atoms
   are; an application's own (its module's) before the system's */
PCLS find_class(LPCSTR name, HINSTANCE hinst)
{
    HMODULE m = module_of(hinst);
    ATOM a = name_atom(name);
    PCLS c, sys = 0;
    if (!a)
        return 0;
    for (c = (PCLS)classes; c; c = (PCLS)c->next) {
        if (c->atom != a)
            continue;
        if (c->hModule == m && m && !IS_SYSTEM(c))
            return c;
        if (c->style & CS_GLOBALCLASS)
            sys = c;
        if (IS_SYSTEM(c))
            sys = sys ? sys : c;
    }
    return sys;
}

LPCSTR class_menu_name(PCLS c)
{
    LPCSTR n = c->lpszMenuName;
    if (!ISATOM(n))
        n = (LPCSTR)UH_PTR(&uhStr, LOWORD((DWORD)n));  /* the heap's selector now */
    return n;
}

static PCLS register_class(const WNDCLASS __far *wc, BOOL system)
{
    PCLS c;
    HMODULE m = system ? hModUser : module_of(wc->hInstance);
    ATOM a = name_atom(wc->lpszClassName);
    WORD menu = 0;
    if (a)
        for (c = (PCLS)classes; c; c = (PCLS)c->next)
            if (c->atom == a && c->hModule == m)
                return 0;               /* already registered by this module */
    a = int_name(wc->lpszClassName);
    if (!a)
        a = AddAtom(wc->lpszClassName);
    if (!a)
        return 0;
    if (wc->lpszMenuName && !ISATOM(wc->lpszMenuName))
        menu = uh_strdup(wc->lpszMenuName);
    c = menu || !wc->lpszMenuName || ISATOM(wc->lpszMenuName) ?
        (PCLS)LocalAlloc(LPTR, sizeof(CLS) + (wc->cbClsExtra > 0 ? wc->cbClsExtra : 0)) : (PCLS)0;
    if (!c) {
        uh_free(&uhStr, menu);
        if (a >= 0xC000u)
            DeleteAtom(a);
        return 0;
    }
    c->sig = CLS_SIG;
    c->atom = a;
    c->style = wc->style;
    c->lpfnWndProc = wc->lpfnWndProc;
    c->cbClsExtra = wc->cbClsExtra > 0 ? wc->cbClsExtra : 0;
    c->cbWndExtra = wc->cbWndExtra > 0 ? wc->cbWndExtra : 0;
    c->hModule = m;
    c->hIcon = wc->hIcon;
    c->hCursor = wc->hCursor;
    c->hbrBackground = wc->hbrBackground;
    c->lpszMenuName = menu ? (LPCSTR)UH_PTR(&uhStr, menu) : wc->lpszMenuName;
    c->next = classes;
    classes = (WORD)c;
    return c;
}

/* The class's atom for a program that expects Windows 3.10 or later, 1
   for an older one (3.0's RegisterClass returned a BOOL): probe US16 on
   3.1, Standard and 386 Enhanced, KRNL386 and KRNL286, for string,
   "#nnnnn" and integer-atom names, CS_GLOBALCLASS and a local "Button"
   class; Pietrek (Windows Internals, ch. 4): "either 0, 1, or the atom
   handle of the class name". The version is the registering module's
   (wc->hInstance's). */
ATOM WINAPI RegisterClass(const WNDCLASS __far *wc)
{
    PCLS c;
    c = register_class(wc, FALSE);
    if (!c)
        return 0;
    return K_GETEXPWINVER(K_GETEXEPTR(wc->hInstance)) >= 0x030A ? c->atom : 1;
}

BOOL WINAPI UnregisterClass(LPCSTR name, HINSTANCE hinst)
{
    PCLS c = find_class(name, hinst);
    WORD *link;
    if (!c || IS_SYSTEM(c) || c->cwnd)
        return FALSE;
    for (link = &classes; *link; link = &((PCLS)*link)->next)
        if (*link == (WORD)c) {
            *link = c->next;
            break;
        }
    if (!ISATOM(c->lpszMenuName))
        uh_free(&uhStr, LOWORD((DWORD)c->lpszMenuName));
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
    /* 3.1: the hInstance asked with (US15); 0 for a system class */
    wc->hInstance = IS_SYSTEM(c) ? 0 : hinst ? hinst : (HINSTANCE)c->hModule;
    wc->hIcon = c->hIcon;
    wc->hCursor = c->hCursor;
    wc->hbrBackground = c->hbrBackground;
    wc->lpszMenuName = class_menu_name(c);
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
        return (LONG)class_menu_name(c);
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
extern LONG WINAPI __loadds DesktopWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI __loadds MenuWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI __loadds IconTitleWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI __loadds MDIClientWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI __loadds ButtonWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI __loadds StaticWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI __loadds EditWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI __loadds ListBoxWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI __loadds ComboLBoxWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI __loadds ComboBoxWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI __loadds ScrollBarWndProc(HWND, UINT, WPARAM, LPARAM);
extern LONG WINAPI DefDlgProc(HWND, UINT, WPARAM, LPARAM);

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
    sysclass("Button", 0x008B, (WNDPROC)ButtonWndProc, 3, 0, IDC_ARROW);
    sysclass("Edit", 0x0088, (WNDPROC)EditWndProc, 6, 0, IDC_IBEAM);
    sysclass("Static", 0x0080, (WNDPROC)StaticWndProc, 6, 0, IDC_ARROW);
    sysclass("ListBox", 0x0088, (WNDPROC)ListBoxWndProc, 2, 0, IDC_ARROW);
    sysclass("ScrollBar", 0x008B, (WNDPROC)ScrollBarWndProc, 10, 0, IDC_ARROW);
    sysclass("ComboBox", 0x0088, (WNDPROC)ComboBoxWndProc, 2, 0, IDC_ARROW);
    sysclass("ComboLBox", 0x0808, (WNDPROC)ComboLBoxWndProc, 2, 0, IDC_ARROW);
    sysclass(MAKEINTRESOURCE(32770), 0x2808, (WNDPROC)DefDlgProc, DLGWINDOWEXTRA, 0, IDC_ARROW);
    sysclass("MDIClient", 0, (WNDPROC)MDIClientWndProc, 16,
             (HBRUSH)(COLOR_APPWORKSPACE + 1), IDC_ARROW);
}
