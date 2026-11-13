/*
 * util.c - w16 COMMDLG: shared pieces: start-up, the extended error,
 * memory, strings, messages, templates, hooks, DOS.
 */
#include "commdlg.h"

HINSTANCE cd_inst;
UINT msg_lbselch, msg_sharevi, msg_fileok, msg_colorok, msg_setrgb, msg_findmsg, msg_help;

/* CommDlgExtendedError: the last call's code. One value for every task,
   as one DLL data segment serves all of them. */
static DWORD last_error;

void cd_seterr(DWORD e)
{
    last_error = e;
}

DWORD cd_geterr(void)
{
    return last_error;
}

DWORD CDAPI CommDlgExtendedError(void)
{
    return last_error;
}

/* the library's start (entry.asm) */
int __cdecl __far cd_libmain(HINSTANCE inst)
{
    cd_inst = inst;
    msg_lbselch = RegisterWindowMessage(S_LBSELCH);
    msg_sharevi = RegisterWindowMessage(S_SHAREVI);
    msg_fileok = RegisterWindowMessage(S_FILEOK);
    msg_colorok = RegisterWindowMessage(S_COLOROK);
    msg_setrgb = RegisterWindowMessage(S_SETRGB);
    msg_findmsg = RegisterWindowMessage(S_FINDMSG);
    msg_help = RegisterWindowMessage(S_HELP);
    return 1;
}

int CDAPI WEP(int how)
{
    (void)how;
    pics_free();
    return 1;
}

/* ---- memory: global blocks, locked for their life ---- */

LPVOID mem_alloc(DWORD n)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, n ? n : 1);
    LPVOID p;
    if (!h)
        return NULL;
    p = GlobalLock(h);
    if (!p)
        GlobalFree(h);
    return p;
}

static HGLOBAL handle_of(LPVOID p)
{
    DWORD h = GlobalHandle(SELECTOROF(p));
    return (HGLOBAL)LOWORD(h);
}

LPVOID mem_realloc(LPVOID p, DWORD n)
{
    LPVOID q;
    if (!p)
        return mem_alloc(n);
    q = mem_alloc(n);
    if (!q)
        return NULL;
    {
        DWORD old = GlobalSize(handle_of(p)), k;
        BYTE FAR *d = (BYTE FAR *)q, FAR *s = (BYTE FAR *)p;
        if (old > n)
            old = n;
        for (k = 0; k < old; k++)
            d[(WORD)k] = s[(WORD)k];
    }
    mem_free(p);
    return q;
}

void mem_free(LPVOID p)
{
    HGLOBAL h;
    if (!p)
        return;
    h = handle_of(p);
    GlobalUnlock(h);
    GlobalFree(h);
}

void mem_zero(LPVOID p, WORD n)
{
    BYTE FAR *b = (BYTE FAR *)p;
    while (n--)
        *b++ = 0;
}

void mem_copy(LPVOID d, const void FAR *s, WORD n)
{
    BYTE FAR *a = (BYTE FAR *)d;
    const BYTE FAR *b = (const BYTE FAR *)s;
    while (n--)
        *a++ = *b++;
}

/* ---- strings ---- */

int s_len(LPCSTR s)
{
    int n = 0;
    if (s)
        while (s[n])
            n++;
    return n;
}

void s_cpy(LPSTR d, LPCSTR s, int size)
{
    if (size <= 0)
        return;
    while (--size > 0 && *s)
        *d++ = *s++;
    *d = 0;
}

void s_cat(LPSTR d, LPCSTR s, int size)
{
    int n = s_len(d);
    s_cpy(d + n, s, size - n);
}

LPSTR s_dup(LPCSTR s)
{
    int n = s_len(s);
    LPSTR r = mem_alloc(n + 1);
    if (r)
        s_cpy(r, s ? s : "", n + 1);
    return r;
}

int s_icmp(LPCSTR a, LPCSTR b)
{
    return lstrcmpi(a, b);
}

LPSTR s_chr(LPCSTR s, char c)
{
    for (; *s; s = AnsiNext(s))
        if (*s == c)
            return (LPSTR)s;
    return NULL;
}

LPSTR s_rchr(LPCSTR s, char c)
{
    LPCSTR r = NULL;
    for (; *s; s = AnsiNext(s))
        if (*s == c)
            r = s;
    return (LPSTR)r;
}

void s_lower(LPSTR s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z')
            *s += 'a' - 'A';
}

void s_upper(LPSTR s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s -= 'a' - 'A';
}

int s_atoi(LPCSTR s, BOOL FAR *ok)
{
    unsigned v = 0;
    BOOL good = *s != 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            good = FALSE;
            break;
        }
        v = v * 10 + (*s - '0');
    }
    if (ok)
        *ok = good;
    return (int)v;
}

void s_itoa(int v, LPSTR d)
{
    char t[8];
    int i = 0;
    unsigned u = v < 0 ? -v : v;
    do {
        t[i++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0)
        *d++ = '-';
    while (i)
        *d++ = t[--i];
    *d = 0;
}

/* ---- resources and messages ---- */

LPSTR load_str(UINT ids)
{
    int size = 64, n;
    LPSTR s = NULL, grown;
    for (;;) {                          /* as long as the string is */
        grown = mem_realloc(s, size);
        if (!grown) {
            mem_free(s);
            return NULL;
        }
        s = grown;
        n = LoadString(cd_inst, ids, s, size);
        if (n < size - 1)
            return s;
        size *= 2;
    }
}

/* a message box: string ids with "%s" (arg) or "%c" formatted */
static int msgbox_fmt(HWND owner, LPCSTR caption, UINT ids, LPCSTR arg, char c, UINT style)
{
    LPSTR fmt = load_str(ids), text;
    int r = 0;
    if (!fmt)
        return 0;
    text = mem_alloc(s_len(fmt) + s_len(arg) + 2);
    if (text) {
        if (arg)
            wsprintf(text, fmt, arg);
        else
            wsprintf(text, fmt, (int)c);
        r = MessageBox(owner, text, caption, style);
        mem_free(text);
    }
    mem_free(fmt);
    return r;
}

int cd_msgbox(HWND owner, LPCSTR caption, UINT ids, LPCSTR arg, UINT style)
{
    return msgbox_fmt(owner, caption, ids, arg ? arg : "", 0, style);
}

int cd_msgbox_c(HWND owner, LPCSTR caption, UINT ids, char drive, UINT style)
{
    return msgbox_fmt(owner, caption, ids, NULL, drive, style);
}

/* ---- templates ---- */


static HGLOBAL get_template(const cd_template FAR *t, BOOL FAR *own_res)
{
    HRSRC r;
    HGLOBAL h;
    *own_res = TRUE;
    if (t->use_handle) {
        if (!t->app) {
            cd_seterr(CDERR_NOHINSTANCE);
            return NULL;
        }
        *own_res = FALSE;
        return (HGLOBAL)t->app;
    }
    if (t->use_name) {
        if (!t->app) {
            cd_seterr(CDERR_NOHINSTANCE);
            return NULL;
        }
        r = FindResource(t->app, t->name, RT_DIALOG);
    } else {
        r = FindResource(cd_inst, t->ours, RT_DIALOG);
    }
    if (!r) {
        cd_seterr(CDERR_FINDRESFAILURE);
        return NULL;
    }
    h = LoadResource(t->use_name ? t->app : cd_inst, r);
    if (!h)
        cd_seterr(CDERR_LOADRESFAILURE);
    return h;
}

int cd_modal(const cd_template FAR *t, HWND owner, FARPROC proc, LPARAM lp)
{
    BOOL res;
    HGLOBAL h = get_template(t, &res);
    LPCSTR p;
    int r;
    if (!h)
        return -1;
    p = res ? LockResource(h) : GlobalLock(h);
    if (!p) {
        cd_seterr(CDERR_LOCKRESFAILURE);
        if (res)
            FreeResource(h);
        return -1;
    }
    r = DialogBoxIndirectParam(cd_inst, (HGLOBAL)h, owner, (DLGPROC)proc, lp);
    if (res)
        UnlockResource(h);
    else
        GlobalUnlock(h);
    if (res)
        FreeResource(h);
    if (r == -1)
        cd_seterr(CDERR_DIALOGFAILURE);
    return r;
}

HWND cd_modeless(const cd_template FAR *t, HWND owner, FARPROC proc, LPARAM lp)
{
    BOOL res;
    HGLOBAL h = get_template(t, &res);
    LPCSTR p;
    HWND w;
    if (!h)
        return NULL;
    p = res ? LockResource(h) : GlobalLock(h);
    if (!p) {
        cd_seterr(CDERR_LOCKRESFAILURE);
        if (res)
            FreeResource(h);
        return NULL;
    }
    w = CreateDialogIndirectParam(cd_inst, p, owner, (DLGPROC)proc, lp);
    if (res) {
        UnlockResource(h);
        FreeResource(h);
    } else {
        GlobalUnlock(h);
    }
    if (!w)
        cd_seterr(CDERR_DIALOGFAILURE);
    return w;
}

/* ---- state, hooks, help ---- */

void state_set(HWND dlg, LPVOID st)
{
    /* the property holds the block's selector (protected mode) or
       handle; the offset is always 0 (mem_alloc) */
    SetProp(dlg, PROP_STATE, (HANDLE)SELECTOROF(st));
}

LPVOID state_get(HWND dlg)
{
    WORD sel = (WORD)GetProp(dlg, PROP_STATE);
    return sel ? MAKELP(sel, 0) : NULL;
}

void state_remove(HWND dlg)
{
    RemoveProp(dlg, PROP_STATE);
}

UINT call_hook(cd_hook h, HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    return h ? h(dlg, msg, wp, lp) : 0;
}

void send_help(HWND dlg, HWND owner, LPARAM lp)
{
    if (owner)
        SendMessage(owner, msg_help, (WPARAM)dlg, lp);
}

/* ---- subclassing: the class's own procedure is the original ---- */

void subclass(HWND w, FARPROC proc)
{
    if (w)
        SetWindowLong(w, GWL_WNDPROC, (LONG)proc);
}

LRESULT call_old(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return CallWindowProc((FARPROC)GetClassLong(w, GCL_WNDPROC), w, msg, wp, lp);
}

/* ---- DOS ---- */

static int last_dos_err;

int dos_err(void)
{
    return last_dos_err;
}

int dos_getdrive(void)
{
    int d = 0;
    _asm {
        mov ah, 19h
        int 21h
        xor ah, ah
        mov d, ax
    }
    return d;
}

void dos_setdrive(int d)
{
    _asm {
        mov dl, byte ptr d
        mov ah, 0Eh
        int 21h
    }
}

BOOL dos_chdir(LPCSTR path)
{
    int e = 0;
    _asm {
        push ds
        lds dx, path
        mov ah, 3Bh
        int 21h
        pop ds
        jnc ok
        mov e, ax
    ok:
    }
    last_dos_err = e;
    return e == 0;
}

BOOL dos_getcwd(int drive, LPSTR buf)
{
    int e = 0;
    _asm {
        push ds
        push si
        lds si, buf
        mov dl, byte ptr drive
        mov ah, 47h
        int 21h
        pop si
        pop ds
        jnc ok
        mov e, ax
    ok:
    }
    last_dos_err = e;
    return e == 0;
}

int dos_findfirst(LPCSTR spec, int attr, LPVOID dta)
{
    int e = 0;
    _asm {
        push ds
        lds dx, dta
        mov ah, 1Ah
        int 21h
        pop ds
        push ds
        lds dx, spec
        mov cx, attr
        mov ah, 4Eh
        int 21h
        pop ds
        jnc ok
        mov e, ax
    ok:
    }
    last_dos_err = e;
    return e;
}

int dos_findnext(LPVOID dta)
{
    int e = 0;
    _asm {
        push ds
        lds dx, dta
        mov ah, 1Ah
        int 21h
        pop ds
        mov ah, 4Fh
        int 21h
        jnc ok
        mov e, ax
    ok:
    }
    last_dos_err = e;
    return e;
}

int dos_getattr(LPCSTR path)
{
    int e = 0, a = 0;
    _asm {
        push ds
        lds dx, path
        mov ax, 4300h
        int 21h
        pop ds
        jnc ok
        mov e, ax
        jmp done
    ok:
        mov a, cx
    done:
    }
    last_dos_err = e;
    return e ? -1 : a;
}

/* create a new file (fails if it exists); closes it again */
int dos_creat_new(LPCSTR path)
{
    int e = 0;
    _asm {
        push ds
        lds dx, path
        xor cx, cx
        mov ah, 5Bh
        int 21h
        pop ds
        jc bad
        mov bx, ax
        mov ah, 3Eh
        int 21h
        jmp done
    bad:
        mov e, ax
    done:
    }
    last_dos_err = e;
    return e;
}

void dos_delete(LPCSTR path)
{
    _asm {
        push ds
        lds dx, path
        mov ah, 41h
        int 21h
        pop ds
    }
}

/* open (mode: DOS access and sharing byte) and close again */
int dos_open_test(LPCSTR path, int mode)
{
    int e = 0;
    _asm {
        push ds
        lds dx, path
        mov al, byte ptr mode
        mov ah, 3Dh
        int 21h
        pop ds
        jc bad
        mov bx, ax
        mov ah, 3Eh
        int 21h
        jmp done
    bad:
        mov e, ax
    done:
    }
    last_dos_err = e;
    return e;
}

/* is drive d (0 = A:) there? (IOCTL "is removable" answers for every
   drive that exists, 0Fh: invalid drive) */
BOOL dos_drive_exists(int d)
{
    return drive_kind(d) >= 0;
}

LPSTR cur_dir(void)
{
    char buf[DOS_PATH];
    int d = dos_getdrive();
    buf[0] = (char)('A' + d);
    buf[1] = ':';
    buf[2] = '\\';
    if (!dos_getcwd(d + 1, buf + 3))
        buf[3] = 0;
    return s_dup(buf);
}
