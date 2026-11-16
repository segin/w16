/*
 * tk2.c - calling an import KERNEL does not implement must report it and
 * end the program with exit code 0FFh (see kernel/thunk.c).
 * Expected output ends with: w16: call to unimplemented KERNEL.27
 *
 * KERNEL.27 does not exist on Windows 3.1 (docs/exports/KERNEL.txt), so
 * w16 never implements it (tk2.lnk imports it by ordinal). (TK2 used
 * SwapRecording, KERNEL.204, until w16 implemented it.)
 */
#include <windows.h>

extern void __cdecl log_write(const char FAR *buf, WORD len);
extern void FAR PASCAL K_NOSUCH(WORD);

int __cdecl tmain(void)
{
    log_write("TK2 calling KERNEL.27\r\n", 23);
    K_NOSUCH(0);                        /* KERNEL.27: not implemented */
    log_write("FAIL returned from unimplemented API\r\n", 38);
    return 1;
}
