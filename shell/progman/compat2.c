/*
 * compat2.c - replacements for calls Windows 2.x lacks (W16_WIN2 build;
 * see compat2.h). Uses only calls Windows/386 2.11 has.
 */
#include "progman.h"

#ifdef W16_WIN2

/* ---- helpers ---- */

static char up(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* Compare n characters, ignoring case. */
static BOOL same_n(const char FAR *a, const char FAR *b, int n)
{
    while (n--)
        if (up(*a++) != up(*b++))
            return FALSE;
    return TRUE;
}

/* ---- the Windows directory: where WIN.INI is ---- */

UINT FAR pm_GetWindowsDirectory(LPSTR buf, UINT size)
{
    OFSTRUCT of;
    int n;
    if (OpenFile("WIN.INI", &of, OF_EXIST) == HFILE_ERROR) {
        s_cpy(buf, "C:\\", size);
        return s_len(buf);
    }
    s_cpy(buf, of.szPathName, size);
    for (n = s_len(buf); n > 0 && buf[n - 1] != '\\'; n--)
        ;
    if (n > 3)                          /* keep "C:\" for the root */
        n--;
    buf[n] = 0;
    return n;
}

/* ---- private profiles ----
 * The format GetPrivateProfileString reads: [section] lines and
 * key=value lines; names compare without case, blanks around the key
 * and value are dropped. The file is read whole (no size limit). */

static LPSTR profile_path(LPCSTR file)
{
    const char FAR *c;
    char dir[144];                      /* DOS path bound */
    LPSTR p;
    for (c = file; *c; c++)
        if (*c == '\\' || *c == ':')
            return s_dup(file);
    pm_GetWindowsDirectory(dir, sizeof dir);
    p = s_dup(dir);
    if (p && p[s_len(p) - 1] != '\\')
        s_append(&p, "\\");
    if (p)
        s_append(&p, file);
    return p;
}

static LPSTR read_all(LPCSTR path, WORD *len)
{
    OFSTRUCT of;
    HFILE f = OpenFile(path, &of, OF_READ);
    long n;
    LPSTR buf;
    *len = 0;
    if (f == HFILE_ERROR)
        return NULL;
    n = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    if (n < 0 || n > 0xFFF0L || (buf = mem_alloc(n + 1)) == NULL) {
        _lclose(f);
        return NULL;
    }
    *len = _lread(f, buf, (WORD)n);
    if (*len == (WORD)-1)
        *len = 0;
    buf[*len] = 0;
    _lclose(f);
    return buf;
}

/* Find [sec] key in text: returns the offset of the value's first
   character and its length, or -1. Also reports where the section's
   lines end (for inserting) and whether the section exists. */
typedef struct {
    int sec_end;                        /* offset after the section's last line */
    int line, line_end;                 /* the key's whole line */
    int val, val_len;
    BOOL have_sec;
} spot;

static void find(const char FAR *t, int len, LPCSTR sec, LPCSTR key, spot *s)
{
    int i = 0, slen = s_len(sec), klen = key ? s_len(key) : 0;
    BOOL in = FALSE;
    s->sec_end = -1;
    s->line = s->val = -1;
    s->have_sec = FALSE;
    while (i < len) {
        int b = i, e, k;
        while (i < len && t[i] != '\n')
            i++;
        e = i;                          /* end of line (before CR/LF) */
        if (e > b && t[e - 1] == '\r')
            e--;
        if (i < len)
            i++;                        /* past LF */
        for (k = b; k < e && (t[k] == ' ' || t[k] == '\t'); k++)
            ;
        if (k < e && t[k] == '[') {
            int c = k + 1;
            in = FALSE;
            if (e - c > slen && t[c + slen] == ']' && same_n(t + c, sec, slen)) {
                in = TRUE;
                s->have_sec = TRUE;
                s->sec_end = i;
            }
            continue;
        }
        if (!in)
            continue;
        if (k < e)
            s->sec_end = i;
        if (key && e - k > klen) {
            int c = k + klen;
            while (c < e && (t[c] == ' ' || t[c] == '\t'))
                c++;
            if (c < e && t[c] == '=' && same_n(t + k, key, klen) && s->line < 0) {
                c++;
                while (c < e && (t[c] == ' ' || t[c] == '\t'))
                    c++;
                while (e > c && (t[e - 1] == ' ' || t[e - 1] == '\t'))
                    e--;
                s->line = b;
                s->line_end = i;
                s->val = c;
                s->val_len = e - c;
            }
        }
    }
}

int FAR pm_GetPrivateProfileString(LPCSTR sec, LPCSTR key, LPCSTR def, LPSTR buf, int size,
                                   LPCSTR file)
{
    LPSTR path = profile_path(file), text;
    WORD len;
    spot s;
    int n = 0;
    if (size <= 0)
        return 0;
    text = path ? read_all(path, &len) : NULL;
    mem_free(path);
    if (text && key) {
        find(text, len, sec, key, &s);
        if (s.val >= 0) {
            n = s.val_len < size - 1 ? s.val_len : size - 1;
            s_cpy(buf, text + s.val, n + 1);
            mem_free(text);
            return n;
        }
    }
    mem_free(text);
    s_cpy(buf, def ? def : "", size);
    return s_len(buf);
}

UINT FAR pm_GetPrivateProfileInt(LPCSTR sec, LPCSTR key, int def, LPCSTR file)
{
    char buf[16];                       /* a number */
    const char FAR *p = buf;
    if (!pm_GetPrivateProfileString(sec, key, "", buf, sizeof buf, file) ||
        !((buf[0] >= '0' && buf[0] <= '9') || buf[0] == '-'))
        return def;
    return s_atoi(&p);
}

/* Write (value) or delete (value NULL) one key; the file is rewritten. */
BOOL FAR pm_WritePrivateProfileString(LPCSTR sec, LPCSTR key, LPCSTR value, LPCSTR file)
{
    LPSTR path = profile_path(file), text, out = NULL;
    WORD len;
    spot s;
    OFSTRUCT of;
    HFILE f;
    BOOL ok = FALSE;
    if (!path || !key)
        goto done;
    text = read_all(path, &len);
    if (!text) {
        text = s_dup("");
        len = 0;
    }
    if (!text)
        goto done;
    find(text, len, sec, key, &s);
    out = s_dup("");
    if (out) {
        int cut, resume;
        if (s.line >= 0) {              /* replace or remove that line */
            cut = s.line;
            resume = s.line_end;
        } else if (s.have_sec) {        /* add at the end of the section */
            cut = resume = s.sec_end;
        } else {                        /* new section at the end */
            cut = resume = len;
        }
        {
            char keep = text[cut];      /* append text[0..cut) */
            text[cut] = 0;
            ok = s_append(&out, text);
            text[cut] = keep;
        }
        if (ok && !s.have_sec && value) {
            if (len && text[len - 1] != '\n')
                ok = s_append(&out, "\r\n");
            ok = ok && s_append(&out, "\r\n[") && s_append(&out, sec) && s_append(&out, "]\r\n");
        }
        if (ok && value)
            ok = s_append(&out, key) && s_append(&out, "=") && s_append(&out, value) &&
                 s_append(&out, "\r\n");
        if (ok && resume < (int)len)
            ok = s_append(&out, text + resume);
    }
    mem_free(text);
    if (ok) {
        f = OpenFile(path, &of, OF_CREATE | OF_WRITE);
        ok = f != HFILE_ERROR;
        if (ok) {
            ok = _lwrite(f, out, s_len(out)) == (WORD)s_len(out);
            _lclose(f);
        }
    }
done:
    mem_free(out);
    mem_free(path);
    return ok;
}

/* ---- starting programs ---- */

typedef struct {                        /* LoadModule's parameter block */
    WORD env;                           /* 0: a copy of ours */
    LPSTR cmdline;                      /* length byte, text, CR */
    WORD FAR *show;                     /* { 2, nCmdShow } */
    DWORD reserved;
} loadparms;

UINT FAR pm_WinExec(LPCSTR cmd, UINT show)
{
    LPSTR file, tail;
    WORD shw[2];
    loadparms lp;
    const char FAR *c = cmd;
    int n = 0, t;
    HINSTANCE h;

    while (*c == ' ')
        c++;
    while (c[n] && c[n] != ' ')
        n++;
    file = mem_alloc(n + 5);
    if (!file)
        return 0;
    s_cpy(file, c, n + 1);
    {                                   /* WinExec's default extension */
        int k;
        BOOL ext = FALSE;
        for (k = 0; file[k]; k++)
            if (file[k] == '.')
                ext = TRUE;
            else if (file[k] == '\\')
                ext = FALSE;
        if (!ext)
            s_cat(file, ".EXE", n + 5);
    }
    c += n;
    t = s_len(c);                       /* DOS tails are at most 126 bytes */
    if (t > 126)
        t = 126;
    tail = mem_alloc(t + 2);
    if (!tail) {
        mem_free(file);
        return 0;
    }
    tail[0] = (char)t;
    s_cpy(tail + 1, c, t + 1);
    tail[t + 1] = '\r';
    {
        /* Observed: Windows 2.x's loader puts up a system-modal "Change
           Disk ... insert in drive A:" box when the file is missing,
           which blocks everything; check first and report "not found"
           (2) like WinExec does. */
        OFSTRUCT of;
        if (OpenFile(file, &of, OF_EXIST) == HFILE_ERROR) {
            mem_free(file);
            mem_free(tail);
            return 2;
        }
    }
    shw[0] = 2;
    shw[1] = show;
    lp.env = 0;
    lp.cmdline = tail;
    lp.show = shw;
    lp.reserved = 0;
    h = LoadModule(file, &lp);
    mem_free(file);
    mem_free(tail);
    return (UINT)h;
}

/* The program for a document, from WIN.INI [extensions]: "ext=prog ^.ext". */
HINSTANCE FAR pm_FindExecutable(LPCSTR file, LPCSTR dir, LPSTR result)
{
    const char FAR *e = file + s_len(file);
    char ext[4], assoc[144];
    int n;
    (void)dir;
    result[0] = 0;
    while (e > file && e[-1] != '.' && e[-1] != '\\')
        e--;
    if (e == file || e[-1] != '.')
        return (HINSTANCE)31;
    s_cpy(ext, e, sizeof ext);
    if (!GetProfileString("extensions", ext, "", assoc, sizeof assoc))
        return (HINSTANCE)31;
    for (n = 0; assoc[n] && assoc[n] != ' '; n++)
        ;
    assoc[n] = 0;
    s_cpy(result, assoc, 144);
    return (HINSTANCE)33;
}

HINSTANCE FAR pm_ShellExecute(HWND w, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, int show)
{
    char prog[144];
    LPSTR cmd;
    UINT r;
    (void)w;
    (void)op;
    (void)dir;
    /* a program runs as it is; a document through its associated program */
    if ((UINT)pm_FindExecutable(file, NULL, prog) <= 32) {
        cmd = s_dup(file);
    } else {
        cmd = s_dup(prog);
        if (cmd)
            s_append(&cmd, " ");
        if (cmd)
            s_append(&cmd, file);
    }
    {
        const char FAR *e = file + s_len(file);
        while (e > file && e[-1] != '.')
            e--;
        if (e > file && (!s_icmp(e, "EXE") || !s_icmp(e, "COM") || !s_icmp(e, "BAT") ||
                         !s_icmp(e, "PIF"))) {
            mem_free(cmd);
            cmd = s_dup(file);
        }
    }
    if (cmd && params && params[0]) {
        s_append(&cmd, " ");
        s_append(&cmd, params);
    }
    r = cmd ? pm_WinExec(cmd, show) : 0;
    mem_free(cmd);
    return (HINSTANCE)r;
}

/* ---- window placement: the parts Program Manager uses ---- */

BOOL FAR pm_GetWindowPlacement(HWND w, WINDOWPLACEMENT FAR *wp)
{
    RECT r;
    POINT o;
    HWND parent = GetParent(w);
    GetWindowRect(w, &r);
    o.x = o.y = 0;
    if (parent)
        ClientToScreen(parent, &o);
    OffsetRect(&r, -o.x, -o.y);
    wp->flags = 0;
    wp->showCmd = IsIconic(w) ? SW_SHOWMINIMIZED : IsZoomed(w) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    wp->ptMinPosition.x = wp->ptMinPosition.y = -1;
    wp->ptMaxPosition.x = wp->ptMaxPosition.y = -1;
    if (IsIconic(w)) {
        wp->ptMinPosition.x = r.left;
        wp->ptMinPosition.y = r.top;
        wp->flags = WPF_SETMINPOSITION;
        /* TODO: remember the normal rectangle of minimized windows */
    }
    wp->rcNormalPosition = r;
    return TRUE;
}

BOOL FAR pm_SetWindowPlacement(HWND w, const WINDOWPLACEMENT FAR *wp)
{
    (void)w;
    (void)wp;                           /* TODO: minimized positions */
    return TRUE;
}

/* ---- menus: ChangeMenu, the 2.x way ---- */

BOOL FAR pm_ModifyMenu(HMENU m, UINT pos, UINT flags, UINT id, LPCSTR text)
{
    return ChangeMenu(m, pos, text, id, MF_CHANGE | (flags & ~MF_BYPOSITION));
}

BOOL FAR pm_WinHelp(HWND w, LPCSTR file, UINT cmd, DWORD data)
{
    (void)file;
    (void)data;
    if (cmd != HELP_QUIT)
        message(w, IDS_NOTYET, NULL);   /* no WinHelp in Windows 2.x */
    return TRUE;
}

#endif

#ifdef W16_WIN2
/* ---- menus: built from our menu resource ----
 * Windows 2.x cannot load the 3.x menu resource format wrc writes (its
 * 4-byte header), so the menu is rebuilt with CreateMenu and ChangeMenu
 * from the same resource: header, then items { WORD flags; WORD id
 * (not for popups); text }, MF_END on the last item of each menu. */

static const BYTE FAR *build_items(HMENU m, const BYTE FAR *p)
{
    for (;;) {
        WORD flags = p[0] | (p[1] << 8), id = 0;
        const char FAR *text;
        p += 2;
        if (!(flags & MF_POPUP)) {
            id = p[0] | (p[1] << 8);
            p += 2;
        }
        text = (const char FAR *)p;
        p += s_len(text) + 1;
        if (flags & MF_POPUP) {
            HMENU sub = CreateMenu();
            p = build_items(sub, p);
            ChangeMenu(m, 0, text, (UINT)sub, MF_APPEND | MF_POPUP);
        } else if (!text[0] && !id) {
            ChangeMenu(m, 0, NULL, 0, MF_APPEND | MF_SEPARATOR);
        } else {
            ChangeMenu(m, 0, text, id,
                       MF_APPEND | MF_STRING | (flags & (MF_GRAYED | MF_DISABLED | MF_CHECKED)));
        }
        if (flags & MF_END)
            return p;
    }
}

HMENU pm_load_menu(HINSTANCE inst, LPCSTR name)
{
    HRSRC r = FindResource(inst, name, RT_MENU);
    HGLOBAL h = r ? LoadResource(inst, r) : NULL;
    const BYTE FAR *p = h ? (const BYTE FAR *)LockResource(h) : NULL;
    HMENU m = NULL;
    if (p) {
        m = CreateMenu();
        build_items(m, p + 4 + (p[2] | (p[3] << 8)));
        UnlockResource(h);
    }
    if (h)
        FreeResource(h);
    return m;
}
#endif
