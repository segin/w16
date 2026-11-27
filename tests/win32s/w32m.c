/*
 * W32M.EXE: a 32-bit (PE, Win32) probe of w16's own for Win32s's memory
 * and exception paths, the ones Win32s's 32-bit kernel serves through its
 * VxD (W32S.386, or w16's replacement: ARCHITECTURE.md 12.20). No C
 * runtime; only documented Win32 calls that Win32s provides (Win32 SDK,
 * "Win32s Programmer's Reference").
 *
 * It writes C:\W32M.LOG, one line per observation ("name value ..."), so
 * that runs under Windows 3.1 with Microsoft's W32S.386 and under w16 can
 * be compared line by line:
 *   - virtual memory: reserve, commit, VirtualQuery of each state,
 *     protection changes, decommit, release; a reservation at a chosen
 *     address; MEM_COMMIT without MEM_RESERVE; a large sparse reservation
 *   - exceptions caught by a frame of the probe's own (FS:[0], no C
 *     runtime): access to reserved, read-only and decommitted pages, a
 *     null pointer, INT 3, a division by zero, a privileged instruction;
 *     each one's code, flags, address and the faulting address
 *   - the stack growing 192 KB deep
 *   - the process heap, HeapCreate's heaps, GlobalAlloc
 *   - a named file mapping of the paging file, mapped twice
 * then "W32M end". Addresses are written as they come: they are
 * Win32s's own choice and compared where they are deterministic.
 *
 * Built like W32T.EXE (tests/win32ssetup.sh): wcc386 -bt=nt -zl, wlink
 * system nt_win, start=_w32mmain, kernel32 and user32.
 */
#include <windows.h>

static HANDLE logf;

static void put(const char *s)
{
    DWORD n = 0, w;

    while (s[n])
        n++;
    WriteFile(logf, s, n, &w, NULL);
}

static void hex(DWORD v)
{
    char b[10];
    int i;

    for (i = 7; i >= 0; i--) {
        b[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    b[8] = ' ';
    b[9] = 0;
    put(b);
}

static void line(const char *name, int n, DWORD a, DWORD b, DWORD c, DWORD d)
{
    put(name);
    put(" ");
    if (n > 0) hex(a);
    if (n > 1) hex(b);
    if (n > 2) hex(c);
    if (n > 3) hex(d);
    put("\r\n");
}

/* VirtualQuery of p, as "q.NAME base allocbase allocprot size state prot type" */
static void query(const char *name, void *p)
{
    MEMORY_BASIC_INFORMATION m;
    DWORD r = VirtualQuery(p, &m, sizeof m);

    put("q.");
    put(name);
    put(" ");
    hex(r);
    hex((DWORD)m.BaseAddress);
    hex((DWORD)m.AllocationBase);
    hex(m.AllocationProtect);
    hex(m.RegionSize);
    hex(m.State);
    hex(m.Protect);
    hex(m.Type);
    put("\r\n");
}

/* ---- exceptions: a frame of our own on FS:[0] (w32mx.asm) ---- */
static DWORD x_code, x_flags, x_addr, x_info0, x_info1, x_n;
extern DWORD x_resume, x_esp, x_ebp;
typedef void (__cdecl *probe_fn)(DWORD);
void __cdecl run_guarded(probe_fn f, DWORD arg);
void __cdecl p_int3(DWORD a);
void __cdecl p_div0(DWORD a);
void __cdecl p_cli(DWORD a);
void __cdecl p_hlt(DWORD a);

EXCEPTION_DISPOSITION __cdecl handler(EXCEPTION_RECORD *er, void *frame,
                                      CONTEXT *cx, void *dc)
{
    (void)frame; (void)dc;
    x_code = er->ExceptionCode;
    x_flags = er->ExceptionFlags;
    x_addr = (DWORD)er->ExceptionAddress;
    x_n = er->NumberParameters;
    x_info0 = x_n > 0 ? er->ExceptionInformation[0] : 0;
    x_info1 = x_n > 1 ? er->ExceptionInformation[1] : 0;
    cx->Eip = x_resume;
    cx->Esp = x_esp;
    cx->Ebp = x_ebp;
    return ExceptionContinueExecution;
}

/* run f(arg) under our handler; "x.NAME code flags address n info0 info1"
 * (code 0: no exception) */
static void try_it(const char *name, probe_fn f, DWORD arg)
{
    x_code = x_flags = x_addr = x_info0 = x_info1 = x_n = 0;
    run_guarded(f, arg);
    put("x.");
    put(name);
    put(" ");
    hex(x_code);
    hex(x_flags);
    hex(x_addr);
    hex(x_n);
    hex(x_info0);
    hex(x_info1);
    put("\r\n");
}

static void __cdecl p_read(DWORD a) { volatile DWORD v = *(volatile DWORD *)a; (void)v; }
static void __cdecl p_write(DWORD a) { *(volatile DWORD *)a = 0x12345678; }

/* the stack 192 KB deep: 48 frames of 4 KB, each page touched */
static DWORD deep(DWORD n)
{
    volatile char b[4000];
    b[0] = (char)n;
    b[3999] = (char)n;
    return n ? deep(n - 1) + b[0] : b[3999];
}

static void vm_section(void)
{
    char *r, *p, *q;
    DWORD old = 0, ok;

    /* reserve 64 KB, then look at it */
    r = VirtualAlloc(NULL, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
    line("reserve", 1, (DWORD)r, 0, 0, 0);
    if (!r)
        return;
    query("reserved", r);
    try_it("readreserved", p_read, (DWORD)r);
    /* commit the first 8 KB */
    p = VirtualAlloc(r, 0x2000, MEM_COMMIT, PAGE_READWRITE);
    line("commit", 2, (DWORD)p, GetLastError(), 0, 0);
    query("committed", r);
    query("rest", r + 0x2000);
    line("fresh", 2, *(DWORD *)r, *(DWORD *)(r + 0x1FFC), 0, 0);
    *(DWORD *)r = 0xA5A5A5A5;
    *(DWORD *)(r + 0x1FFC) = 0x5A5A5A5A;
    line("readback", 2, *(DWORD *)r, *(DWORD *)(r + 0x1FFC), 0, 0);
    /* read-only */
    ok = VirtualProtect(r, 0x1000, PAGE_READONLY, &old);
    line("protect", 2, ok, old, 0, 0);
    query("readonly", r);
    try_it("writereadonly", p_write, (DWORD)r);
    line("afterwrite", 1, *(DWORD *)r, 0, 0, 0);
    ok = VirtualProtect(r, 0x1000, PAGE_NOACCESS, &old);
    line("noaccess", 2, ok, old, 0, 0);
    query("noaccess", r);
    try_it("readnoaccess", p_read, (DWORD)r);
    ok = VirtualProtect(r, 0x1000, PAGE_READWRITE, &old);
    line("readwrite", 2, ok, old, 0, 0);
    line("kept", 1, *(DWORD *)r, 0, 0, 0);
    /* decommit the second page */
    ok = VirtualFree(r + 0x1000, 0x1000, MEM_DECOMMIT);
    line("decommit", 1, ok, 0, 0, 0);
    query("decommitted", r + 0x1000);
    try_it("readdecommitted", p_read, (DWORD)(r + 0x1000));
    /* commit it again: zeroed */
    q = VirtualAlloc(r + 0x1000, 0x1000, MEM_COMMIT, PAGE_READWRITE);
    line("recommit", 2, (DWORD)q, *(DWORD *)(r + 0x1FFC), 0, 0);
    ok = VirtualFree(r, 0, MEM_RELEASE);
    line("release", 1, ok, 0, 0, 0);
    query("released", r);

    /* commit without reserving */
    p = VirtualAlloc(NULL, 0x3000, MEM_COMMIT, PAGE_READWRITE);
    line("commitonly", 1, (DWORD)p, 0, 0, 0);
    if (p) {
        query("commitonly", p);
        line("zero", 1, *(DWORD *)(p + 0x2FFC), 0, 0, 0);
        VirtualFree(p, 0, MEM_RELEASE);
    }

    /* at a chosen address */
    p = VirtualAlloc((void *)0x20000000, 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    line("fixed", 2, (DWORD)p, GetLastError(), 0, 0);
    if (p) {
        query("fixed", p);
        VirtualFree(p, 0, MEM_RELEASE);
    }

    /* 16 MB reserved, a page committed here and there */
    r = VirtualAlloc(NULL, 0x1000000, MEM_RESERVE, PAGE_NOACCESS);
    line("big", 1, (DWORD)r, 0, 0, 0);
    if (r) {
        DWORD i;
        for (i = 0; i < 16; i++) {
            p = VirtualAlloc(r + i * 0x100000 + 0x5000, 0x1000, MEM_COMMIT, PAGE_READWRITE);
            *(DWORD *)p = i;
        }
        query("big0", r);
        query("big5", r + 0x5000);
        query("big6", r + 0x6000);
        line("bigsum", 2, *(DWORD *)(r + 0xF05000), *(DWORD *)(r + 0x105000), 0, 0);
        VirtualFree(r, 0, MEM_RELEASE);
    }

    /* bad requests */
    p = VirtualAlloc(NULL, 0, MEM_RESERVE, PAGE_NOACCESS);
    line("size0", 2, (DWORD)p, GetLastError(), 0, 0);
    ok = VirtualFree((void *)0x12345000, 0, MEM_RELEASE);
    line("freebad", 2, ok, GetLastError(), 0, 0);
}

static void exc_section(void)
{
    try_it("null", p_read, 0);
    try_it("nullwrite", p_write, 0);
    try_it("high", p_read, 0xF0000000);
    try_it("int3", p_int3, 0);
    try_it("div0", p_div0, 0);
    try_it("cli", p_cli, 0);
    try_it("hlt", p_hlt, 0);
    line("deep", 1, deep(48), 0, 0, 0);
}

static void heap_section(void)
{
    HANDLE h = GetProcessHeap(), h2;
    char *a, *b;
    HGLOBAL g;

    a = HeapAlloc(h, HEAP_ZERO_MEMORY, 100);
    line("heapalloc", 2, a != NULL, a ? *(DWORD *)a : 0, 0, 0);
    line("heapsize", 1, a ? HeapSize(h, 0, a) : 0, 0, 0, 0);
    if (a) HeapFree(h, 0, a);
    h2 = HeapCreate(0, 0x10000, 0x100000);
    line("heapcreate", 1, h2 != NULL, 0, 0, 0);
    if (h2) {
        b = HeapAlloc(h2, 0, 0x8000);
        line("heap2", 1, b != NULL, 0, 0, 0);
        if (b) {
            b[0] = 1;
            b[0x7FFF] = 2;
            line("heap2rw", 2, b[0], b[0x7FFF], 0, 0);
        }
        line("heapdestroy", 1, HeapDestroy(h2), 0, 0, 0);
    }
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0x3000);
    a = g ? GlobalLock(g) : NULL;
    line("global", 3, g != 0, a != NULL, a ? *(DWORD *)(a + 0x2FFC) : 0, 0);
    if (g) {
        GlobalUnlock(g);
        GlobalFree(g);
    }
}

static void map_section(void)
{
    HANDLE m;
    DWORD *v1, *v2;

    m = CreateFileMappingA((HANDLE)0xFFFFFFFF, NULL, PAGE_READWRITE, 0, 0x20000, "W32MMAP");
    line("mapping", 1, m != NULL, 0, 0, 0);
    if (!m)
        return;
    v1 = MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, 0);
    v2 = MapViewOfFile(m, FILE_MAP_READ, 0, 0x10000, 0x10000);
    line("views", 2, (DWORD)v1, (DWORD)v2, 0, 0);
    if (v1 && v2) {
        v1[0x10000 / 4] = 0xCAFEF00D;
        line("shared", 2, v2[0], v1[0], 0, 0);
        query("view1", v1);
        query("view2", v2);
        try_it("writeview2", p_write, (DWORD)v2);
    }
    if (v2) UnmapViewOfFile(v2);
    if (v1) UnmapViewOfFile(v1);
    CloseHandle(m);
}

void __cdecl w32mmain(void)
{
    logf = CreateFileA("C:\\W32M.LOG", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (logf == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    line("W32M start", 1, GetVersion(), 0, 0, 0);
    vm_section();
    exc_section();
    heap_section();
    map_section();
    put("W32M end\r\n");
    CloseHandle(logf);
    ExitProcess(0);
}
