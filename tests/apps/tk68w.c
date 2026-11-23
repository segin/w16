/*
 * tk68w.c - TK68W.EXE: stands in for WINOLDAP.MOD and WINOA386.MOD (and is
 * run as a Windows program too) for TK68 (tests/apps/tk68.c), and appends
 * to C:\TK68.LOG what KERNEL gave it: ES (the PDB) against GetCurrentPDB,
 * the PDB selector's limit, BX, the length byte at 80h, and the bytes from
 * 81h to 81h + the length byte (the byte that ends the tail included),
 * no further than FFh unless an LF is there, printable characters as they
 * are and the others as \xx.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */
extern WORD __cdecl entry_regs[11];  /* ax bx cx dx si di bp sp ds es ss */

static char line[2400];
static int ln;

static void put(const char *s)
{
    while (*s && ln < (int)sizeof line - 4)
        line[ln++] = *s++;
}

static void hex(WORD v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--)
        line[ln++] = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
}

static void flush(void)
{
    HFILE f = _lopen("C:\\TK68.LOG", OF_READWRITE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\TK68.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    line[ln++] = '\r';
    line[ln++] = '\n';
    _lwrite(f, line, ln);
    _lclose(f);
    ln = 0;
}

int __cdecl tmain(void)
{
    WORD pdb = init_regs[6], i;
    BYTE FAR *p = MAKELP(pdb, 0);
    DWORD lim = GetSelectorLimit(pdb);
    WORD end;

    put("W es=pdb:");
    put(pdb == GetCurrentPDB() ? "yes" : "no");
    put(" entry-es=pdb:");
    put(entry_regs[9] == pdb ? "yes" : "no");
    put(" lim=");
    hex((WORD)lim, 4);
    put(" bx=");
    hex(init_regs[1], 4);
    put(" len=");
    hex(p[0x80], 2);
    flush();
    put("W [");
    /* the tail and what ends it: up to 81h + the length byte when an LF
       is there (WINOLDAP's), else no further than FFh (past it lies
       whatever follows the PDB on 3.1) */
    end = 0x81 + p[0x80];
    if (end > lim || p[end] != '\n')
        end = end > 0xFF ? 0xFF : end;
    for (i = 0x81; i <= end; i++) {
        BYTE c = p[i];
        if (c >= 0x20 && c < 0x7F && c != '\\')
            line[ln++] = c;
        else {
            line[ln++] = '\\';
            hex(c, 2);
        }
    }
    put("] n=");
    hex(i - 0x81, 4);
    flush();
    return 0;
}
