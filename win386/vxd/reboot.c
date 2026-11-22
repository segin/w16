/* reboot.c - w16's REBOOT device (device 0009h), a module of WIN386.EXE's
 * W3 table (ARCHITECTURE.md 12.8): Ctrl+Alt+Del and KERNEL's PM API.
 *
 * Ctrl+Alt+Del is a VKD hot key (VKD_Define_Hot_Key at Device_Init). With
 * no application hung, Windows 3.1 shows a text screen (observed with
 * tests/scripts/input/win386cad.txt): blue, the title in a grey box, the
 * explanation and "Press any key to continue"; a key returns to Windows
 * with the screen exactly as it was, Ctrl+Alt+Del again restarts the
 * computer. w16 shows the same screen ("w16" for "Windows" in its text,
 * ARCHITECTURE.md 3): it saves the VGA's registers, planes and DAC, sets
 * text mode through the System VM's BIOS (Exec_Int), waits for a key on
 * the keyboard controller, then sets the old mode again and restores.
 * KERNEL's handler (set with 0201h) and its sanity byte tell a hung
 * application from none: the byte zeroed, KERNEL's procedure run in the
 * VM, and the byte set again (screen A) or a 0203h call from it for the
 * task that does not respond (screen B); neither: screen C (reboot_cad).
 * The Ctrl and Alt releases read while a screen is up go to the VM with
 * VKD_Force_Keys.
 *
 * The PM API (VX5; Ralf Brown's Interrupt List 61, INT 2Fh 1684h BX=0009h):
 * 0201h sets KERNEL's Ctrl+Alt+Del handler (ES:DI) and its sanity-check
 * byte (DS:SI), 0202h gives the handler back; function 0 fails (VX3). The
 * others (0100h warm boot, 0203h, 0204h) fail, carry set (UNVERIFIED).
 * Sources: the DDK's Virtual Device Adaptation Guide (VKD_Define_Hot_Key,
 * the nested execution services); IBM VGA register descriptions; the BIOS
 * (INT 10h 00h, 02h).
 */
#include "w16vxd.h"

void __cdecl vkd_define_hot_key(struct vregs *r);
void __cdecl vkd_force_keys(struct vregs *r);
void __cdecl vmm_map_flat(struct vregs *r);
void __cdecl vmm_build_int_stack_frame(struct vregs *r);
void __cdecl vmm_set_global_time_out(struct vregs *r);
void __cdecl vmm_cancel_time_out(struct vregs *r);
extern char rb_tmo[];
#pragma aux rb_tmo "*"
void __cdecl vmm_begin_nest_v86_exec(struct vregs *r);
void __cdecl vmm_exec_int(struct vregs *r);
void __cdecl vmm_end_nest_exec(struct vregs *r);
void __cdecl vmm_save_client_state(struct vregs *r);
void __cdecl vmm_restore_client_state(struct vregs *r);
void __cdecl vmm_get_sys_vm(struct vregs *r);
void __cdecl vmm_get_cur_vm(struct vregs *r);
void __cdecl vmm_get_exec_focus(struct vregs *r);

/* the VMM's half (w16vxd.h): a DOS VM's title, closing it */
struct reboot_link reboot_link = { REBOOT_LINK_MAGIC, 0, 0 };
extern char rb_cad[];
#pragma aux rb_cad "*"

static void outb(vu32 port, vu32 v);
static vu32 inb(vu32 port);
#pragma aux outb = "out dx, al" parm [edx] [eax] modify exact [];
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];

static void zero(void *p, vu32 n) { vu8 *q = p; while (n--) *q++ = 0; }
static void copy(void *d, const void *s, vu32 n) { vu8 *p = d; const vu8 *q = s; while (n--) *p++ = *q++; }

static vu16 kern_sel, kern_off, sane_sel, sane_off;    /* 0201h */
static vu8 *sane;                       /* the sanity byte (Map_Flat of 0201h's DS:SI) */
/* the sanity check (reboot_cad); 0: screen A at once on Ctrl+Alt+Del */
#define SANITY_CHECK 1
static int cad_state;                   /* 2: KERNEL's procedure due at an idle, 4: due now, 3: a screen up */
static int del_down;                    /* Del held (the hot key's press and release callbacks) */
static vu32 tmo_handle;
static vu32 sys_vm, client;

/* ---- the VGA's state ---- */
static vu8 seq[5], gc[9], crtc[25], attr[21], misc, dac[768];
/* the index registers and the DAC's mask and write index: display
 * drivers leave an index set (the Sequencer at Map Mask, the Graphics
 * Controller at Bit Mask) and then write only the data port in their
 * loops. Left at other values after the screen, Windows 3.1's VGA.DRV
 * drew every bitmap from memory without its colours (icons, File
 * Manager's: black and white) until it set them again */
static vu8 seq_i, gc_i, crtc_i, attr_i, dac_mask, dac_w;
static vu32 crt;                        /* the CRTC: 3D4h, or 3B4h in a monochrome setting (Misc bit 0 clear) */
static vu8 planes[4][65536];

/* the VGA's memory whichever VM runs: through the System VM's high
 * linear window (a windowed DOS VM's own A0000h-BFFFFh is RAM) */
static vu8 *vga_mem(void)
{
    return (vu8 *)(CB_HIGH_LINEAR(sys_vm) + 0xA0000);
}

static void vga_save(void)
{
    vu32 i, p;
    seq_i = inb(0x3C4);
    gc_i = inb(0x3CE);
    crt = (inb(0x3CC) & 1) ? 0x3D4 : 0x3B4;
    crtc_i = inb(crt);
    inb(crt + 6);
    attr_i = inb(0x3C0);                    /* with its palette-access bit */
    dac_mask = inb(0x3C6);
    dac_w = inb(0x3C8);
    misc = inb(0x3CC);
    for (i = 0; i < 5; i++) { outb(0x3C4, i); seq[i] = inb(0x3C5); }
    for (i = 0; i < 9; i++) { outb(0x3CE, i); gc[i] = inb(0x3CF); }
    for (i = 0; i < 25; i++) { outb(crt, i); crtc[i] = inb(crt + 1); }
    for (i = 0; i < 21; i++) { inb(crt + 6); outb(0x3C0, i | 0x20); attr[i] = inb(0x3C1); }
    inb(crt + 6);
    outb(0x3C7, 0);
    for (i = 0; i < 768; i++) dac[i] = inb(0x3C9);
    /* the planes as they are: odd/even and chain 4 off, A0000h 64 KB
     * (a text mode maps B8000h 32 KB with odd/even, mode 13h chains the
     * planes: read as they were, the font and text were lost) */
    outb(0x3C4, 4); outb(0x3C5, 0x06);
    outb(0x3CE, 5); outb(0x3CF, 0);         /* read mode 0 */
    outb(0x3CE, 6); outb(0x3CF, 0x05);
    for (p = 0; p < 4; p++) {
        outb(0x3CE, 4); outb(0x3CF, p);     /* read map p */
        copy(planes[p], (void *)vga_mem(), 65536);
    }
}

static void vga_restore(void)
{
    vu32 i, p;
    /* the planes, with plain writes */
    outb(0x3C4, 4); outb(0x3C5, 0x06);      /* not chain 4, not odd/even */
    outb(0x3CE, 5); outb(0x3CF, 0);         /* write mode 0 */
    outb(0x3CE, 1); outb(0x3CF, 0);         /* no set/reset */
    outb(0x3CE, 3); outb(0x3CF, 0);         /* no rotate, replace */
    outb(0x3CE, 8); outb(0x3CF, 0xFF);      /* every bit */
    outb(0x3CE, 6); outb(0x3CF, 0x05);      /* A0000h, 64 KB, graphics */
    for (p = 0; p < 4; p++) {
        outb(0x3C4, 2); outb(0x3C5, 1u << p);
        copy((void *)vga_mem(), planes[p], 65536);
    }
    /* the registers as they were */
    outb(0x3C2, misc);
    outb(0x3C4, 0); outb(0x3C5, 1);         /* synchronous reset */
    for (i = 1; i < 5; i++) { outb(0x3C4, i); outb(0x3C5, seq[i]); }
    outb(0x3C4, 0); outb(0x3C5, seq[0] | 3);
    outb(crt, 0x11); outb(crt + 1, crtc[0x11] & 0x7F);  /* unprotect 0-7 */
    for (i = 0; i < 25; i++) { outb(crt, i); outb(crt + 1, crtc[i]); }
    for (i = 0; i < 9; i++) { outb(0x3CE, i); outb(0x3CF, gc[i]); }
    inb(crt + 6);
    for (i = 0; i < 21; i++) { outb(0x3C0, i); outb(0x3C0, attr[i]); }
    outb(0x3C0, 0x20);                      /* the screen on again */
    outb(0x3C8, 0);
    for (i = 0; i < 768; i++) outb(0x3C9, dac[i]);
    /* the indexes as the driver left them */
    outb(0x3C6, dac_mask);
    outb(0x3C8, dac_w);
    inb(crt + 6);
    outb(0x3C0, attr_i | 0x20);
    outb(crt, crtc_i);
    outb(0x3CE, gc_i);
    outb(0x3C4, seq_i);
}

/* INT 10h in the System VM with AX, BX, DX */
static void bios_video(vu32 ax, vu32 bx, vu32 dx)
{
    struct vregs r;
    struct client_regs *c = (struct client_regs *)client;
    zero(&r, sizeof(r));
    c->eax = ax; c->ebx = bx; c->edx = dx;
    c->eflags = 0x0202;
    r.eax = 0x10;
    r.ebp = client;
    vmm_exec_int(&r);
}

/* ---- the screens (Windows 3.1's layout: row, column, text; measured
 * from its screenshots, ARCHITECTURE.md 12.8; "w16" for "Windows" where
 * it names the product). A: Ctrl+Alt+Del with nothing hung
 * (tests/scripts/input/win386cad.txt); B: a task that does not respond,
 * its module's name in the title (KERNEL's call, 0203h; tests/vx8run.sh
 * with VX8 hanging); C: the system did not answer (seen when the sanity
 * byte was not KERNEL's: VX8 mode 1) ---- */
struct line { vu8 row, col; const char *s; };
static const struct line lines_a[] = {
    { 8, 5, "Although you can use CTRL+ALT+DEL to quit an application that has" },
    { 9, 5, "stopped responding to the system, there is no application in this" },
    { 10, 5, "state." },
    { 12, 5, "To quit an application, use the application's quit or exit command," },
    { 13, 5, "or choose the Close command from the Control menu." },
    { 15, 5, "*  Press any key to return to w16." },
    { 16, 5, "*  Press CTRL+ALT+DEL again to restart your computer. You will" },
    { 17, 5, "   lose any unsaved information in all applications." },
    { 19, 27, "Press any key to continue" },
    { 0, 0, 0 }
};
static const struct line lines_b[] = {
    { 10, 8, "This Windows application has stopped responding to the system." },
    { 12, 5, "*  Press ESC to cancel and return to w16." },
    { 13, 5, "*  Press ENTER to close this application that is not responding." },
    { 14, 8, "You will lose any unsaved information in this application." },
    { 15, 5, "*  Press CTRL+ALT+DEL again to restart your computer. You will" },
    { 16, 8, "lose any unsaved information in all applications." },
    { 18, 21, "Press ENTER for OK or ESC to Cancel: OK" },
    { 0, 0, 0 }
};
static const struct line lines_c[] = {
    { 10, 5, "The system is either busy or has become unstable.  You can wait and" },
    { 11, 5, "see if the system becomes available again and continue working or you" },
    { 12, 5, "can restart your computer." },
    { 14, 5, "*  Press any key to return to w16 and wait." },
    { 15, 5, "*  Press CTRL+ALT+DEL again to restart your computer. You will" },
    { 16, 8, "lose any unsaved information in all applications." },
    { 18, 27, "Press any key to continue" },
    { 0, 0, 0 }
};
#define SCREEN_A 0
#define SCREEN_B 1
#define SCREEN_C 2
#define SCREEN_D 3
/* D: a DOS program in front (Windows 3.1, tests/scripts/input/
 * vx12gc.txt): its window's title on row 6; Esc back to it, Enter closes
 * it (the VM ends, Windows comes back) */
static const struct line lines_d[] = {
    { 8, 5, "You can use CTRL+ALT+DEL to quit an application that has stopped" },
    { 9, 5, "responding to the system. When possible, you should quit the" },
    { 10, 5, "application by using the method recommended in the documentation" },
    { 11, 5, "provided with the application." },
    { 13, 5, "*  Press ESC to cancel and return to the non-Windows application." },
    { 14, 5, "*  Press ENTER to close this application that is not responding." },
    { 15, 8, "You will lose any unsaved information in this application." },
    { 16, 5, "*  Press CTRL+ALT+DEL again to restart your computer. You will" },
    { 17, 8, "lose any unsaved information in all applications." },
    { 19, 21, "Press ENTER for OK or ESC to Cancel: OK" },
    { 0, 0, 0 }
};

static void put(vu32 row, vu32 col, const char *s, vu8 attr)
{
    vu16 *v = (vu16 *)(vga_mem() + 0x18000) + row * 80 + col;
    while (*s) *v++ = (vu16)((attr << 8) | (vu8)*s++);
}

/* the title, centred as Windows 3.1's: " Windows " (9) at column 35,
 * " VX8.EXE " (9) at 35, " PROGMAN.EXE " (13) at 33 */
static void title(vu32 row, const char *name)
{
    char t[24];
    vu32 n = 0;
    t[n++] = ' ';
    while (*name && n < 21) t[n++] = *name++;
    t[n++] = ' ';
    t[n] = 0;
    put(row, 40 - (n + 1) / 2, t, 0x71);
}

static void show_screen(int kind, const char *name)
{
    vu16 *v = (vu16 *)(vga_mem() + 0x18000);
    const struct line *l = kind == SCREEN_A ? lines_a : kind == SCREEN_B ? lines_b
                         : kind == SCREEN_D ? lines_d : lines_c;
    vu32 i;
    for (i = 0; i < 80 * 25; i++) v[i] = 0x1F20;
    if (kind == SCREEN_A) title(6, "w16");             /* Windows 3.1: " Windows " */
    else if (kind == SCREEN_D) title(6, name);
    else title(8, kind == SCREEN_B ? name : "WARNING!");
    for (; l->s; l++) put(l->row, l->col, l->s, 0x1F);
}

/* the next key from the controller; returns its scan code; *cad:
 * Ctrl+Alt+Del again. Windows 3.1 (tests/scripts/input/win386cad.txt,
 * keys held and let go one by one) keeps the screen while the keys of
 * the hot key are held (the keyboard repeats Del meanwhile) and let go,
 * and goes back on a new key. So releases, the repeats of a key still
 * held (a make code with no break since) and Shift, Ctrl and Alt presses
 * are only tracked; Ctrl, Alt and Del count as held at entry. A byte from
 * the pointing device (status bit 5, the auxiliary output buffer; IBM
 * PS/2 keyboard controller) is not a key: mouse motion on a machine whose
 * controller passes it through port 60h must not end the screen. Screen
 * B takes only Enter and Esc (Windows 3.1: Space did nothing). A release
 * of a key pressed before the screen (Ctrl, Alt) is kept for the VM. */
static vu8 held[128], seen[128];
static vu8 orphans[16];
static vu32 n_orphans;

static vu32 wait_key(int kind, int *cad)
{
    vu32 b, code, st, i;
    int e0;
    for (i = 0; i < 128; i++) held[i] = seen[i] = 0;
    /* Ctrl and Alt count as held (their repeats and releases are only
     * tracked either way); Del as the hot key last saw it: a screen shown
     * from the time-out may come after Del was let go, and a new Del
     * must then count */
    held[0x1D] = held[0x38] = 1;
    held[0x53] = (vu8)del_down;
    n_orphans = 0;
    e0 = 0;
    for (;;) {
        while (!((st = inb(0x64)) & 1))
            ;
        b = inb(0x60);
        if (st & 0x20) continue;            /* the mouse's */
        if (b == 0xE0) { e0 = 1; continue; }
        if (b == 0xE1) continue;
        code = b & 0x7F;
        if (b & 0x80) {
            /* pressed before: the VM's (an extended key's with its E0h:
             * the right Ctrl's release is not the left's) */
            if (!seen[code] && code != 0x53 && n_orphans + 2 <= sizeof(orphans)) {
                if (e0) orphans[n_orphans++] = 0xE0;
                orphans[n_orphans++] = (vu8)b;
            }
            e0 = 0;
            held[code] = 0;
            continue;
        }
        e0 = 0;
        if (held[code]) continue;           /* a repeat */
        held[code] = seen[code] = 1;
        if (code == 0x1D || code == 0x38 || code == 0x2A || code == 0x36) continue;
        *cad = (code == 0x53 && held[0x1D] && held[0x38]);
        if (*cad) return code;
        if ((kind == SCREEN_B || kind == SCREEN_D) && code != 0x01 && code != 0x1C) continue;
        return code;
    }
}

/* a screen until its key; returns the key's scan code */
static vu32 screen(int kind, const char *name)
{
    static struct client_regs saved;
    struct vregs s;
    vu8 mode;
    vu32 key;
    int cad = 0;

    zero(&s, sizeof(s));
    s.edi = (vu32)&saved;
    vmm_save_client_state(&s);
    /* the BIOS calls run in the current VM (Exec_Int), with its client
     * registers: when a DOS VM was current, the System VM's were written
     * and the current VM's used */
    zero(&s, sizeof(s));
    vmm_get_cur_vm(&s);
    client = CB_CLIENT(s.ebx);
    zero(&s, sizeof(s));
    vmm_begin_nest_v86_exec(&s);
    mode = *(vu8 *)0x449;                   /* the BIOS's video mode */
    vga_save();
    bios_video(0x0003, 0, 0);
    show_screen(kind, name);
    /* the cursor: after the prompt (A, C), on its "OK" (B) */
    bios_video(0x0200, 0, kind == SCREEN_A ? (19 << 8) | 53 : kind == SCREEN_B ? (18 << 8) | 58
                          : kind == SCREEN_D ? (19 << 8) | 58 : (18 << 8) | 53);
    key = wait_key(kind, &cad);
    del_down = held[0x53];
    if (cad) {
        outb(0x64, 0xFE);                   /* a warm boot: the controller's reset line */
        for (;;)
            ;
    }
    bios_video(mode, 0, 0);
    vga_restore();
    /* the VM saw Ctrl and Alt go down (VKD passed them on before Del, the
     * hot key): their releases read here go to it now (VKD_Force_Keys),
     * or its keyboard driver would keep them down (tests/scripts/input/
     * win386cad.txt: Enter afterwards opens File Manager as under Windows
     * 3.1). UNVERIFIED how Windows 3.1 gives them back */
    if (n_orphans) {
        /* Ctrl's release first, then Alt's, then the rest (Windows 3.1:
         * 9Dh B8h whichever was let go first; VX11 K and VX12 K) */
        static vu8 sorted[16];
        vu32 pass, i, n = 0;
        for (pass = 0; pass < 3; pass++)
            for (i = 0; i < n_orphans; i++) {
                vu32 e0 = orphans[i] == 0xE0, b = e0 ? orphans[i + 1] : orphans[i];
                vu32 k = (b & 0x7F) == 0x1D ? 0 : (b & 0x7F) == 0x38 ? 1 : 2;
                if (k == pass) {
                    if (e0) sorted[n++] = 0xE0;
                    sorted[n++] = (vu8)b;
                }
                if (e0) i++;
            }
        for (i = 0; i < n; i++) orphans[i] = sorted[i];
        zero(&s, sizeof(s));
        s.esi = (vu32)orphans;
        s.ecx = n_orphans;
        vkd_force_keys(&s);
    }
    zero(&s, sizeof(s));
    vmm_end_nest_exec(&s);
    zero(&s, sizeof(s));
    s.esi = (vu32)&saved;
    vmm_restore_client_state(&s);
    return key;
}

/* a screen with no other Ctrl+Alt+Del taken meanwhile: from the time-out
 * (IRQ 0's VTD procedure, the IRQ already ended on the PIC) the VM's BIOS
 * calls (Exec_Int, interrupts on) let IRQ 1 in, and a Del the keyboard
 * repeated while Ctrl and Alt were still down was a new hot key press:
 * it started the check again, and its time-out showed screen A a second
 * time after the key had taken the first away (2 of 12 remote win386cad
 * runs, "Space did not dismiss the screen") */
static vu32 screen_once(int kind, const char *name)
{
    vu32 key;
    cad_state = 3;
    key = screen(kind, name);
    cad_state = 0;
    return key;
}

static void screen_a(void)
{
    screen_once(SCREEN_A, 0);
}

static void time_out(vu32 ms)
{
    struct vregs s;
    zero(&s, sizeof(s));
    s.eax = ms;
    s.esi = (vu32)rb_tmo;
    vmm_set_global_time_out(&s);
    tmo_handle = s.esi;
}

static void cancel_time_out(void)
{
    struct vregs s;
    zero(&s, sizeof(s));
    s.esi = tmo_handle;
    vmm_cancel_time_out(&s);
    tmo_handle = 0;
}

/* the hot key callback (AL scan code, AH 0 pressed). With KERNEL's
 * handler known (0201h): the sanity byte zeroed and KERNEL's procedure
 * run; a time-out then tells (UNVERIFIED: Windows 3.1's delay; 750 ms
 * here): the byte set again, screen A; else screen C */
void __cdecl reboot_cad(struct vregs *r)
{
    struct vregs s;
    if ((r->eax >> 8) & 0xFF) { del_down = 0; return; }    /* the release */
    del_down = 1;
    if (cad_state) return;
    {   /* a DOS program in front: its own screen at once (Windows 3.1) */
        struct vregs f;
        static char t[32];
        zero(&f, sizeof(f));
        vmm_get_exec_focus(&f);
        if (f.ebx && f.ebx != sys_vm) {
            vu32 vm = f.ebx;
            if (!reboot_link.title || !reboot_link.title(vm, t, sizeof t)) {
                t[0] = 0;
            }
            if (screen_once(SCREEN_D, t[0] ? t : "Non-Windows application") == 0x1C && reboot_link.close)
                reboot_link.close(vm);
            return;
        }
    }
    if (!sane || !SANITY_CHECK) { screen_a(); return; }
    *sane = 0;
    /* KERNEL's procedure as an interrupt of the VM (Windows 3.1's Reboot
     * device calls Build_Int_Stack_Frame: docs/win386-services.md), made
     * by the VMM when the VM next idles; it sets the byte again. If the VM
     * has not idled after 750 ms the procedure runs where the VM is, and
     * for a task that does not respond calls 0203h (screen B). Waiting
     * for KERNEL to set the byte by itself (at a task switch or idle
     * pass) left it 0 after 750 ms in a third of the runs under load */
    cad_state = 2;
    time_out(750);
    zero(&s, sizeof(s));
    s.ecx = kern_sel;
    s.edx = kern_off;
    vmm_build_int_stack_frame(&s);
}

/* the time-out (Set_Global_Time_Out) */
void __cdecl reboot_time_out(struct vregs *r)
{
    (void)r;
    tmo_handle = 0;
    if (cad_state == 2 && *sane) { screen_a(); return; }
    if (cad_state == 2) {
        /* the VM has not idled: KERNEL's procedure now, where it is (the
         * VMM makes the frame at once on a second call) */
        struct vregs s;
        zero(&s, sizeof(s));
        s.ecx = kern_sel;
        s.edx = kern_off;
        vmm_build_int_stack_frame(&s);
        cad_state = 4;
        time_out(750);
        return;
    }
    if (cad_state == 4 && *sane) { screen_a(); return; }
    if (cad_state == 4) {                   /* KERNEL's handler did not answer (UNVERIFIED delay) */
        screen_once(SCREEN_C, 0);
        return;
    }
    cad_state = 0;
}

#define SYS_CRITICAL_INIT 0x00
#define DEVICE_INIT       0x01

void __cdecl reboot_control(struct vregs *r)
{
    struct vregs s;
    r->eflags &= ~VF_CF;
    if (r->eax == SYS_CRITICAL_INIT)
        sys_vm = r->ebx;
    else if (r->eax == DEVICE_INIT) {
        /* Ctrl+Alt+Del: Del (53h, normal or extended), Ctrl and Alt down
         * (w16 VKD's shift bits: Ctrl 2, Alt 4), on the press (1) and
         * the release (2: Del's state for the screens) */
        zero(&s, sizeof(s));
        s.eax = 0xFF53;
        s.ebx = (6u << 16) | 6;
        s.ecx = 3;
        s.esi = (vu32)rb_cad;
        vkd_define_hot_key(&s);
    }
}

/* the PM API */
void __cdecl reboot_api(struct vregs *r)
{
    struct client_regs *c = (struct client_regs *)r->ebp;
    switch (c->eax & 0xFFFF) {
    case 0x0201:                            /* set KERNEL's Ctrl+Alt+Del handler */
        kern_sel = c->es; kern_off = (vu16)c->edi;
        sane_sel = c->ds; sane_off = (vu16)c->esi;
        {
            struct vregs s;
            zero(&s, sizeof(s));
            s.eax = (0x3C << 8) | 0x04;     /* Client_DS : Client_ESI */
            vmm_map_flat(&s);
            sane = s.eax == 0xFFFFFFFFu ? 0 : (vu8 *)s.eax;
        }
        c->eflags &= ~VF_CF;
        return;
    case 0x0203:                            /* KERNEL: a task does not respond (ES:DI its module's name) */
        /* observed: KERNEL's handler, run for a hung task (VX8), calls
         * this with ES:DI "VX8.EXE", DS:SI its sanity byte's place, BX 7Dh,
         * DX a selector (UNVERIFIED meanings); with carry back it ended
         * Windows. Esc: AX 0, carry clear: KERNEL goes back to the task,
         * as under Windows 3.1. Enter: AX 1 (UNVERIFIED; 2 did the same):
         * KERNEL ends the task with INT 21h 4Ch, which the DPMI host
         * still takes as KRNL386's own end (Windows ends: the open bug in
         * the handover note) */
        {
            struct vregs s;
            const char *name;
            zero(&s, sizeof(s));
            s.eax = (0x38 << 8) | 0x00;     /* Client_ES : Client_EDI */
            vmm_map_flat(&s);
            name = s.eax == 0xFFFFFFFFu ? "" : (const char *)s.eax;
            cancel_time_out();              /* KERNEL answered: no screen C */
            c->eax = (c->eax & 0xFFFF0000u) | (screen_once(SCREEN_B, name) == 0x1C ? 1 : 0);
            c->eflags &= ~VF_CF;
        }
        return;
    case 0x0202:                            /* get it */
        c->es = kern_sel;
        c->edi = (c->edi & 0xFFFF0000u) | kern_off;
        c->eflags &= ~VF_CF;
        return;
    }
    c->eflags |= VF_CF;
}
