/*
 * tk2.c - calling an import KERNEL does not implement must report it and
 * end the program with exit code 0FFh (see kernel/thunk.c).
 * Expected output ends with: w16: call to unimplemented KERNEL.204
 *
 * SwapRecording (KERNEL.204) is used because it only serves swap-tuning
 * tools and w16 has no reason to implement it. If it ever is implemented,
 * pick another ordinal that is not (compare docs/exports/KERNEL.txt with
 * kernel/kernel.lnk) and update tests/run-tests.sh.
 */
#include <windows.h>

extern void __cdecl log_write(const char FAR *buf, WORD len);

int __cdecl tmain(void)
{
    log_write("TK2 calling SwapRecording\r\n", 27);
    SwapRecording(0);                   /* KERNEL.204: not implemented */
    log_write("FAIL returned from unimplemented API\r\n", 38);
    return 1;
}
