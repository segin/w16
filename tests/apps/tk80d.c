/*
 * tk80d.c - TK80D.DLL, put in the system directory as W32SYS.DLL (module
 * W32SYS) for TK80 (tk80.c): its third entry, the one Pietrek has KERNEL
 * go on in to start a PE file, logs what it is given to C:\TK80.LOG:
 *   E t a b c d ...   the current task, then 12 words of the stack from
 *                     the return address (IP, CS) up
 *   S i "text"        each pair of those words (offset, selector) that
 *                     points at readable memory: the text there (up to 40
 *                     characters, up to a NUL or a CR, "." for the unprintable)
 * It returns 2 (file not found; what KERNEL does with an instance is not
 * probed) and takes 5 words off the stack: what Windows 3.1 hands it
 * (TK80) is the program's name (far), its DOS command tail (far: a length
 * byte, the text, CR) and nCmdShow.
 */
#include <windows.h>

int __cdecl libmain(void)
{
    return 1;
}

static void put(const char FAR *s)
{
    HFILE f = _lopen("C:\\TK80.LOG", OF_READWRITE);
    int n = 0;
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\TK80.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    while (s[n])
        n++;
    _lwrite(f, s, n);
    _lclose(f);
}

static char *hex(char *b, WORD v)
{
    int i;
    *b++ = ' ';
    for (i = 12; i >= 0; i -= 4)
        *b++ = "0123456789ABCDEF"[(v >> i) & 15];
    return b;
}

static WORD sel_limit(WORD sel);
#pragma aux sel_limit = ".286p" "lsl ax, bx" "jz l1" "mov ax, 0FFFFh" "l1:" \
    parm [bx] value [ax];
static WORD sel_readable(WORD sel);
#pragma aux sel_readable = ".286p" "mov ax, 1" "verr bx" "jz l1" "xor ax, ax" "l1:" \
    parm [bx] value [ax];

/* (c is the last argument: the lowest on the stack, after the far return
   address) */
UINT FAR PASCAL E3(LPCSTR name, LPCSTR tail, WORD c)
{
    WORD FAR *p = (WORD FAR *)&c - 2;
    char line[100], *q;
    int i, j;
    (void)name; (void)tail;
    line[0] = 'E';
    q = hex(line + 1, (WORD)GetCurrentTask());
    for (i = 0; i < 12; i++)
        q = hex(q, p[i]);
    *q++ = '\r', *q++ = '\n', *q = 0;
    put(line);
    for (i = 0; i + 1 < 12; i++) {
        WORD sel = p[i + 1], off = p[i];
        if (sel < 8 || !sel_readable(sel) || sel_limit(sel) == 0xFFFF || off > sel_limit(sel))
            continue;
        line[0] = 'S';
        q = hex(line + 1, (WORD)i);
        *q++ = ' ', *q++ = '"';
        for (j = 0; j < 40 && (DWORD)off + j <= sel_limit(sel); j++) {
            BYTE ch = ((BYTE FAR *)MAKELP(sel, off))[j];
            if (!ch) break;
            *q++ = ch >= ' ' && ch < 0x7F ? (char)ch : '.';
            if (ch == '\r') break;      /* (a command tail's end) */
        }
        *q++ = '"', *q++ = '\r', *q++ = '\n', *q = 0;
        put(line);
    }
    return 2;
}

int FAR PASCAL WEP(int param)
{
    (void)param;
    return 1;
}
