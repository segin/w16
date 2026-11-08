/*
 * tk2.c - calling an import KERNEL does not implement must report it and
 * end the program with exit code 0FFh (see kernel/thunk.c).
 * Expected output ends with: w16: call to unimplemented KERNEL.5
 */
#include <windows.h>

extern void __cdecl log_write(const char FAR *buf, WORD len);

int __cdecl tmain(void)
{
    log_write("TK2 calling LocalAlloc\r\n", 24);
    LocalAlloc(LMEM_FIXED, 16);         /* KERNEL.5: not implemented yet */
    log_write("FAIL returned from unimplemented API\r\n", 38);
    return 1;
}
