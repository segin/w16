/*
 * W32T.EXE: a minimal 32-bit (PE, Win32) test program for Win32s, for
 * tests/win32ssetup.sh: it confirms that a Win32s install runs a 32-bit
 * program. No C runtime; only documented Win32 calls that Win32s provides
 * (Win32 SDK, "Win32s Programmer's Reference": GetVersion, CreateFileA,
 * SetFilePointer, WriteFile, CloseHandle, MessageBoxA, ExitProcess).
 *
 * It writes C:\W32T.LOG:
 *   W32T start version=XXXXXXXX
 * (GetVersion's result: under Win32s the high bit is set and the low word
 * is the Windows version, 3.10 as 0A03 or 3.11 as 0B03; Win32 SDK,
 * GetVersion), shows a message box, then appends
 *   W32T box=N
 * with MessageBoxA's result (IDOK is 1) and ends.
 *
 * tests/win32ssetup.sh builds it with OpenWatcom's wcc386 and wlink
 * (system nt_win; the relocations are kept, as Win32s needs them).
 */
#include <windows.h>

static void put(HANDLE h, const char *s)
{
    DWORD n = 0, w;

    while (s[n])
        n++;
    WriteFile(h, s, n, &w, NULL);
}

static void hex(char *p, DWORD v)
{
    int i;

    for (i = 7; i >= 0; i--) {
        p[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    p[8] = 0;
}

static HANDLE openlog(DWORD how)
{
    HANDLE h = CreateFileA("C:\\W32T.LOG", GENERIC_WRITE, 0, NULL, how,
                           FILE_ATTRIBUTE_NORMAL, NULL);

    if (h != INVALID_HANDLE_VALUE && how == OPEN_ALWAYS)
        SetFilePointer(h, 0, NULL, FILE_END);
    return h;
}

/* the entry point (wlink option start=_w32tmain); it never returns */
void __cdecl w32tmain(void)
{
    char buf[16];
    HANDLE h;
    int r;

    h = openlog(CREATE_ALWAYS);
    if (h != INVALID_HANDLE_VALUE) {
        hex(buf, GetVersion());
        put(h, "W32T start version=");
        put(h, buf);
        put(h, "\r\n");
        CloseHandle(h);
    }
    r = MessageBoxA(NULL, "A 32-bit program is running.", "W32T",
                    MB_OK | MB_ICONINFORMATION);
    h = openlog(OPEN_ALWAYS);
    if (h != INVALID_HANDLE_VALUE) {
        buf[0] = (char)('0' + r % 10);
        buf[1] = 0;
        put(h, "W32T box=");
        put(h, buf);
        put(h, "\r\n");
        CloseHandle(h);
    }
    ExitProcess(0);
}
