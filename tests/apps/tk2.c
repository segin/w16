/*
 * tk2.c - calling an import KERNEL does not implement must report it and
 * end the program with exit code 0FFh (see kernel/thunk.c).
 * Expected output ends with: w16: call to unimplemented KERNEL.5
 */
#include <windows.h>

int __cdecl tmain(void)
{
    OutputDebugString("TK2 calling LocalAlloc\r\n");
    LocalAlloc(LMEM_FIXED, 16);         /* KERNEL.5: not implemented yet */
    OutputDebugString("FAIL returned from unimplemented API\r\n");
    return 1;
}
