/*
 * md3.c - MD3: mouse stress probe (tests/mousestress.sh,
 * drivers/mouse/README.md). Runs as the shell while the harness moves
 * the pointer back and forth without a pause (xdotool, tests/inputrun.sh)
 * and keeps Windows switching to real mode: phases of DOS file I/O (a
 * file written, read back and deleted through INT 21h, again and again)
 * alternate with idle phases (GetMessage with nothing to do: KERNEL's idle
 * loop, INT 28h). Every 250 ms it reads the cursor position; when it has
 * not changed for [w16] md3stall seconds (default 5) while the pointer is
 * being moved, a stall is logged with the time and what the interrupt
 * controllers and the 8042 say (values of their registers only):
 *   STALL t=SECS mev=N slave isr=.. imr=.. master isr=.. imr=.. kbc=..
 * and when the pointer moves again, RECOVER t=SECS. Every 60 s a line
 *   T t=SECS moves=N io=N
 * and at the end
 *   MD3 done secs=N stalls=N first=SECS moves=N io=N
 * [w16] md3secs: how long (default 600); md3io: seconds of I/O per phase
 * (default 2), md3idle: seconds of idle per phase (default 1);
 * md3reenable=MS: every MS milliseconds the mouse driver's Disable, then
 * its Enable with USER's Mouse_Event (USER.299), as USER does at boot:
 * every Enable is a chance for a packet to come while the driver sets
 * itself up (default 0: never). Counted as "en=N".
 */
#include <windows.h>

#define TK_REF "md1ref.h"
#include "tklog.h"

extern int FAR PASCAL U_INITAPP(WORD hinst);

static BYTE inb(WORD port);
#pragma aux inb = "in al, dx" parm [dx] value [al];
static void outb(WORD port, BYTE v);
#pragma aux outb = "out dx, al" parm [dx] [al];
static WORD pushf_cli(void);
#pragma aux pushf_cli = "pushf" "pop ax" "cli" value [ax];
static void popf_w(WORD f);
#pragma aux popf_w = "push ax" "popf" parm [ax];

static BYTE isr_of(WORD port)
{
    BYTE v;
    WORD f = pushf_cli();
    outb(port, 0x0B);                   /* OCW3: read ISR */
    v = inb(port);
    outb(port, 0x0A);                   /* back to IRR */
    popf_w(f);
    return v;
}

static BYTE buf[2048];

/* MS2 (tests/dos/ms2.asm, run before WIN as "MS2 R": its log wraps)
   loaded? Then at a stall its last records, oldest first (values only:
   kind, AX, BX, w3, w4, INT 15h depth, IVT 74h segment, sequence). */
static void dump_ms2(void)
{
    WORD ivtsel = (WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0000H");
    WORD FAR *ivt = (WORD FAR *)MAKELP(ivtsel, 0);
    WORD off = ivt[0x66 * 2], seg = ivt[0x66 * 2 + 1];
    WORD ls, n, i, maxseq = 0, want;
    WORD FAR *h;
    char line[96];
    if (!seg)
        return;
    ls = AllocSelector(SELECTOROF((void FAR *)buf));
    SetSelectorBase(ls, (DWORD)seg << 4);
    SetSelectorLimit(ls, 0xFFFF);
    h = (WORD FAR *)MAKELP(ls, off);
    if (h[1] == 1500) {
        n = h[0];
        for (i = 0; i < n; i++) {
            WORD q = ((WORD FAR *)MAKELP(ls, h[10] + i * 16))[7];
            if (q > maxseq)
                maxseq = q;
        }
        for (want = maxseq > 60 ? maxseq - 60 : 0; want <= maxseq; want++)
            for (i = 0; i < n; i++) {
                WORD FAR *e = (WORD FAR *)MAKELP(ls, h[10] + i * 16);
                if (e[7] == want) {
                    wsprintf(line, "  R %u %04X %04X %04X %04X d=%u ivt74=%04X #%u\r\n",
                             e[0], e[1], e[2], e[3], e[4], e[5], e[6], e[7]);
                    out(line);
                    break;
                }
            }
    }
    FreeSelector(ls);
}

static void io_burst(void)
{
    HFILE h = _lcreat("C:\\MD3.TMP", 0);
    if (h == HFILE_ERROR)
        return;
    _lwrite(h, (LPCSTR)buf, sizeof buf);
    _llseek(h, 0, 0);
    _lread(h, buf, sizeof buf);
    _lclose(h);
    h = _lopen("C:\\MD3.TMP", OF_READ);
    if (h != HFILE_ERROR) {
        _lread(h, buf, sizeof buf);
        _lclose(h);
    }
}

int __cdecl tmain(void)
{
    char line[160];
    DWORD sec_mark, last_check;
    WORD el = 0, last_change = 0, last_report = 0, phase_end;
    WORD secs, stall_s, io_s, idle_s, stalls = 0, first = 0, in_stall = 0, io_phase = 1;
    DWORD moves = 0, ios = 0, ens = 0, last_en;
    WORD reen;
    FARPROC pdis = NULL, pen = NULL, ume = NULL;
    POINT p0, p;
    MSG m;
    U_INITAPP(init_regs[5]);
    out("MD3 start\r\n");
    secs = GetPrivateProfileInt("w16", "md3secs", 600, "SYSTEM.INI");
    stall_s = GetPrivateProfileInt("w16", "md3stall", 5, "SYSTEM.INI");
    io_s = GetPrivateProfileInt("w16", "md3io", 2, "SYSTEM.INI");
    idle_s = GetPrivateProfileInt("w16", "md3idle", 1, "SYSTEM.INI");
    reen = GetPrivateProfileInt("w16", "md3reenable", 0, "SYSTEM.INI");
    if (reen) {
        HMODULE hm = GetModuleHandle("MOUSE");
        pdis = GetProcAddress(hm, MAKEINTRESOURCE(3));
        pen = GetProcAddress(hm, MAKEINTRESOURCE(2));
        ume = GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(299));
        if (!pdis || !pen || !ume)
            reen = 0;
    }
    SetTimer(0, 0, 50, NULL);           /* GetMessage returns now and then */
    GetCursorPos(&p0);
    sec_mark = last_check = last_en = GetTickCount();
    phase_end = io_s;
    while (el < secs) {
        DWORD t = GetTickCount();
        while (t - sec_mark >= 1000) {   /* whole seconds, no division */
            sec_mark += 1000;
            el++;
        }
        if (el >= phase_end) {
            io_phase = !io_phase;
            phase_end = el + (io_phase ? io_s : idle_s);
        }
        if (io_phase) {
            io_burst();
            ios++;
            while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
                DispatchMessage(&m);
        } else {
            if (GetMessage(&m, 0, 0, 0))
                DispatchMessage(&m);
        }
        if (reen && t - last_en >= reen) {
            last_en = t;
            ((void (FAR PASCAL *)(void))pdis)();
            ((void (FAR PASCAL *)(FARPROC))pen)(ume);
            ens++;
        }
        if (t - last_check >= 250) {
            last_check = t;
            GetCursorPos(&p);
            if (p.x != p0.x || p.y != p0.y) {
                p0 = p;
                moves++;
                last_change = el;
                if (in_stall) {
                    in_stall = 0;
                    wsprintf(line, "RECOVER t=%u\r\n", el);
                    out(line);
                }
            } else if (!in_stall && el - last_change >= stall_s && moves > 0) {
                in_stall = 1;
                stalls++;
                if (stalls == 1)
                    first = el;
                wsprintf(line, "STALL t=%u en=%lu moves=%lu slave isr=%02X imr=%02X master isr=%02X imr=%02X kbc=%02X\r\n",
                         el, ens, moves, isr_of(0xA0), inb(0xA1), isr_of(0x20), inb(0x21), inb(0x64));
                out(line);
                dump_ms2();
            }
        }
        if (el - last_report >= 60) {
            last_report = el;
            wsprintf(line, "T t=%u moves=%lu io=%lu en=%lu\r\n", el, moves, ios, ens);
            out(line);
        }
    }
    wsprintf(line, "MD3 done secs=%u stalls=%u first=%u moves=%lu io=%lu en=%lu\r\n",
             secs, stalls, first, moves, ios, ens);
    out(line);
    OpenFile("C:\\MD3.TMP", (OFSTRUCT FAR *)buf, OF_DELETE);
    return 0;
}
