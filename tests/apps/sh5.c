/*
 * sh5.c - SH5: the program SH4 associates with its test documents. It
 * appends one line to C:\SH5.TXT: its command tail exactly as Windows
 * passed it, its current directory, and nCmdShow, then exits. Not a probe
 * of its own (it has no TK.LOG).
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];       /* ax bx cx dx si di es */
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static char line[400];

/* current drive (0 = A) and directory of drive 0 (current) into p */
static WORD cur_drive(void);
#pragma aux cur_drive = "mov ah, 19h" "int 21h" "xor ah, ah" value [ax];
static void cur_dir(char __far *p);
#pragma aux cur_dir = "push ds" "mov ds, dx" "mov ah, 47h" "xor dl, dl" "int 21h" "pop ds" \
    parm [dx si] modify [ax];

int __cdecl tmain(void)
{
    LPSTR tail = (LPSTR)MAKELP(init_regs[6], 0x81);
    WORD n = 0, i;
    HFILE f;
    OFSTRUCT of;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    lstrcpy(line, "TAIL=[");
    n = lstrlen(line);
    for (i = 0; tail[i] && tail[i] != '\r' && i < 127; i++)
        line[n++] = tail[i];
    line[n] = 0;
    lstrcat(line, "] CWD=[");
    n = lstrlen(line);
    line[n++] = (char)('A' + cur_drive());
    line[n++] = ':';
    line[n++] = '\\';
    cur_dir(line + n);
    lstrcat(line, "] SHOW=");
    n = lstrlen(line);
    line[n++] = (char)('0' + init_regs[3] % 10);
    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = 0;
    f = OpenFile("C:\\SH5.TXT", &of, OF_READWRITE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\SH5.TXT", 0);
    _llseek(f, 0, 2);
    _lwrite(f, line, lstrlen(line));
    _lclose(f);
    return 0;
}
