/*
 * tk69a.c - TK69A.EXE for TK69 (tests/apps/tk69.c): imports ID_GET from
 * TKID (the copy of tests/apps/tkid.c KERNEL found) and writes the id it
 * returns to C:\TK69.ID as one digit.
 */
#include <windows.h>

extern WORD FAR PASCAL ID_GET(void);

int __cdecl tmain(void)
{
    char c = (char)('0' + ID_GET());
    HFILE f = _lcreat("C:\\TK69.ID", 0);
    if (f != HFILE_ERROR) {
        _lwrite(f, &c, 1);
        _lclose(f);
    }
    return 0;
}
