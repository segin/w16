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
 * UNVERIFIED and not done: KERNEL's handler (set with 0201h) and the
 * sanity-check byte, i.e. telling a hung application from none (Windows
 * 3.1's "not responding" path, 0203h), and giving the VM the Ctrl and Alt
 * releases that come while the screen is up (VKD cannot put keys into the
 * VM until port 60h is trapped).
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
void __cdecl vmm_begin_nest_v86_exec(struct vregs *r);
void __cdecl vmm_exec_int(struct vregs *r);
void __cdecl vmm_end_nest_exec(struct vregs *r);
void __cdecl vmm_save_client_state(struct vregs *r);
void __cdecl vmm_restore_client_state(struct vregs *r);
void __cdecl vmm_get_sys_vm(struct vregs *r);
extern char rb_cad[];
#pragma aux rb_cad "*"

static void outb(vu32 port, vu32 v);
static vu32 inb(vu32 port);
#pragma aux outb = "out dx, al" parm [edx] [eax] modify exact [];
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];

static void zero(void *p, vu32 n) { vu8 *q = p; while (n--) *q++ = 0; }
static void copy(void *d, const void *s, vu32 n) { vu8 *p = d; const vu8 *q = s; while (n--) *p++ = *q++; }

static vu16 kern_sel, kern_off, sane_sel, sane_off;    /* 0201h */
static vu32 sys_vm, client;

/* ---- the VGA's state ---- */
static vu8 seq[5], gc[9], crtc[25], attr[21], misc, dac[768];
static vu8 planes[4][65536];

static void vga_save(void)
{
    vu32 i, p;
    misc = inb(0x3CC);
    for (i = 0; i < 5; i++) { outb(0x3C4, i); seq[i] = inb(0x3C5); }
    for (i = 0; i < 9; i++) { outb(0x3CE, i); gc[i] = inb(0x3CF); }
    for (i = 0; i < 25; i++) { outb(0x3D4, i); crtc[i] = inb(0x3D5); }
    for (i = 0; i < 21; i++) { inb(0x3DA); outb(0x3C0, i | 0x20); attr[i] = inb(0x3C1); }
    inb(0x3DA);
    outb(0x3C7, 0);
    for (i = 0; i < 768; i++) dac[i] = inb(0x3C9);
    outb(0x3CE, 5); outb(0x3CF, 0);         /* read mode 0 */
    for (p = 0; p < 4; p++) {
        outb(0x3CE, 4); outb(0x3CF, p);     /* read map p */
        copy(planes[p], (void *)0xA0000, 65536);
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
        copy((void *)0xA0000, planes[p], 65536);
    }
    /* the registers as they were */
    outb(0x3C2, misc);
    outb(0x3C4, 0); outb(0x3C5, 1);         /* synchronous reset */
    for (i = 1; i < 5; i++) { outb(0x3C4, i); outb(0x3C5, seq[i]); }
    outb(0x3C4, 0); outb(0x3C5, seq[0] | 3);
    outb(0x3D4, 0x11); outb(0x3D5, crtc[0x11] & 0x7F);  /* unprotect 0-7 */
    for (i = 0; i < 25; i++) { outb(0x3D4, i); outb(0x3D5, crtc[i]); }
    for (i = 0; i < 9; i++) { outb(0x3CE, i); outb(0x3CF, gc[i]); }
    inb(0x3DA);
    for (i = 0; i < 21; i++) { outb(0x3C0, i); outb(0x3C0, attr[i]); }
    outb(0x3C0, 0x20);                      /* the screen on again */
    outb(0x3C8, 0);
    for (i = 0; i < 768; i++) outb(0x3C9, dac[i]);
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

/* ---- the screen (Windows 3.1's layout: row, column, text; ARCHITECTURE.md 12.8) ---- */
static const struct { vu8 row, col; const char *s; } lines[] = {
    { 8, 5, "Although you can use CTRL+ALT+DEL to quit an application that has" },
    { 9, 5, "stopped responding to the system, there is no application in this" },
    { 10, 5, "state." },
    { 12, 5, "To quit an application, use the application's quit or exit command," },
    { 13, 5, "or choose the Close command from the Control menu." },
    { 15, 5, "*  Press any key to return to w16." },
    { 16, 5, "*  Press CTRL+ALT+DEL again to restart your computer. You will" },
    { 17, 5, "   lose any unsaved information in all applications." },
    { 19, 27, "Press any key to continue" },
};
#define TITLE " w16 "                       /* Windows 3.1: " Windows " */

static void put(vu32 row, vu32 col, const char *s, vu8 attr)
{
    vu16 *v = (vu16 *)0xB8000 + row * 80 + col;
    while (*s) *v++ = (vu16)((attr << 8) | (vu8)*s++);
}

static void show_screen(void)
{
    vu16 *v = (vu16 *)0xB8000;
    vu32 i;
    for (i = 0; i < 80 * 25; i++) v[i] = 0x1F20;
    put(6, 40 - sizeof(TITLE) / 2, TITLE, 0x71);   /* centred: " Windows " at column 35 */
    for (i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
        put(lines[i].row, lines[i].col, lines[i].s, 0x1F);
}

/* the next key press from the controller; returns its scan code; *cad:
 * Ctrl+Alt+Del again. Windows 3.1 (tests/scripts/input/win386cad.txt,
 * keys held and let go one by one) keeps the screen while the keys of
 * the hot key are held (the keyboard repeats Del meanwhile) and let go,
 * and goes back on a new key. So releases, the repeats of a key still
 * held (a make code with no break since) and Shift, Ctrl and Alt presses
 * are only tracked; Ctrl, Alt and Del are held at entry. A byte from the
 * pointing device (status bit 5, the auxiliary output buffer; IBM PS/2
 * keyboard controller) is not a key: mouse motion on a machine whose
 * controller passes it through port 60h must not end the screen. */
static vu8 held[128];

static vu32 wait_key(int *cad)
{
    vu32 b, code, st, i;
    for (i = 0; i < 128; i++) held[i] = 0;
    held[0x1D] = held[0x38] = held[0x53] = 1;   /* still held from the hot key */
    for (;;) {
        while (!((st = inb(0x64)) & 1))
            ;
        b = inb(0x60);
        if (st & 0x20) continue;            /* the mouse's */
        if (b == 0xE0 || b == 0xE1) continue;
        code = b & 0x7F;
        if (b & 0x80) { held[code] = 0; continue; }
        if (held[code]) continue;           /* a repeat */
        held[code] = 1;
        if (code == 0x1D || code == 0x38 || code == 0x2A || code == 0x36) continue;
        *cad = (code == 0x53 && held[0x1D] && held[0x38]);
        return code;
    }
}

/* the hot key callback (AL scan code, AH 0 pressed) */
void __cdecl reboot_cad(struct vregs *r)
{
    static struct client_regs saved;
    struct vregs s;
    vu8 mode;
    int cad = 0;

    if ((r->eax >> 8) & 0xFF) return;       /* only the press */
    zero(&s, sizeof(s));
    s.edi = (vu32)&saved;
    vmm_save_client_state(&s);
    zero(&s, sizeof(s));
    vmm_get_sys_vm(&s);
    client = *(vu32 *)(s.ebx + 8);          /* CB_Client_Pointer */
    zero(&s, sizeof(s));
    vmm_begin_nest_v86_exec(&s);
    mode = *(vu8 *)0x449;                   /* the BIOS's video mode */
    vga_save();
    bios_video(0x0003, 0, 0);
    show_screen();
    bios_video(0x0200, 0, (19 << 8) | 53);  /* the cursor after the prompt */
    wait_key(&cad);
    if (cad) {
        outb(0x64, 0xFE);                   /* a warm boot: the controller's reset line */
        for (;;)
            ;
    }
    bios_video(mode, 0, 0);
    vga_restore();
    /* the VM saw Ctrl and Alt go down (VKD passed them on before Del, the
     * hot key): the releases read here go to it now (VKD_Force_Keys), or
     * its keyboard driver would keep them down (tests/scripts/input/
     * win386cad.txt: Enter afterwards opens File Manager as under Windows
     * 3.1). UNVERIFIED how Windows 3.1 gives them back (the DDK's message
     * mode, VKD_Get_Msg_Key, is not described for this) */
    {
        vu8 rel[2];
        vu32 n = 0;
        if (!held[0x1D]) rel[n++] = 0x9D;
        if (!held[0x38]) rel[n++] = 0xB8;
        if (n) {
            zero(&s, sizeof(s));
            s.esi = (vu32)rel;
            s.ecx = n;
            vkd_force_keys(&s);
        }
    }
    zero(&s, sizeof(s));
    vmm_end_nest_exec(&s);
    zero(&s, sizeof(s));
    s.esi = (vu32)&saved;
    vmm_restore_client_state(&s);
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
         * (w16 VKD's shift bits: Ctrl 2, Alt 4), on the press (1) */
        zero(&s, sizeof(s));
        s.eax = 0xFF53;
        s.ebx = (6u << 16) | 6;
        s.ecx = 1;
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
        c->eflags &= ~VF_CF;
        return;
    case 0x0202:                            /* get it */
        c->es = kern_sel;
        c->edi = (c->edi & 0xFFFF0000u) | kern_off;
        c->eflags &= ~VF_CF;
        return;
    }
    c->eflags |= VF_CF;
}
