/* shell.c - w16 VMM: the SHELL device (0017h; Windows 3.1's W3 module
 * WSHELL), part of the VMM module for now (ARCHITECTURE.md 12.10): DOS
 * VMs for WINOLDAP (WINOA386.MOD, the MS-DOS Prompt and every DOS program
 * started from Windows).
 *
 * SHELL's protected-mode API takes the function in DX (not AX). What
 * WINOA386 calls and gets back, observed under Windows 3.1 with VXS.386
 * (tests/vxd/vxs.asm, tests/vxsrun.sh; the WIN386 handover note, step 6):
 *   0   the version: AX 030Ah, EBX the System VM's handle;
 *   9   ES:DI a block whose 48-bit pointer at +8 (offset, then selector)
 *       is WINOA386's event procedure (1547h:0000h; +4 was 0Fh); EAX,
 *       EBX, ECX back 0 (nonzero: a TSR's name, see shell_api), EDX
 *       back 007Bh: a flat data selector (base 0, 4 GB, ring 3), through
 *       which WINOA386 reads the event records below;
 *   3   create a VM: ES:DI a block (+08h, +0Eh, +14h three 48-bit
 *       pointers, offset then selector: the program's path, its command
 *       tail, the start-up directory, which SHELL turns into linear
 *       addresses in place; +1Eh, +20h the foreground and background
 *       priorities, 100 and 50; +2Eh the title), EAX back the new VM's
 *       handle; Create_VM (EBX the new VM, ESI the block's linear
 *       address, EDI WINOA386's DI);
 *   5   give VM EBX the focus: Set_Device_Focus for VKD, VDD and VMD
 *       (ESI 0F20h, EDI the caller's EDI) to it, or, for a windowed one,
 *       VKD's only (VDD's and VMD's stay with the System VM); the
 *       execution focus; a suspended VM is resumed (VM_Resume), a new
 *       one starts (VM_Critical_Init, VM_Init); a full-screen VM that
 *       loses the focus to the System VM is suspended (VM_Suspend);
 *       EAX 0;
 *   6   the VM's flags: EAX the bits, ECX which (bit 0 windowed); 7 a
 *       status word SHELL keeps in the block (EAX, the low bits);
 *   15h WINOA386's window (ESI) for the VM just created. SHELL calls the
 *       event procedure (hwnd, message, wParam, lParam: a far call in the
 *       System VM, on the VMM's locked stack: 0087h:0FF2h after the
 *       arguments) for its events, lParam the linear address of an event
 *       record (72 bytes apart; +8 the VM's handle, +0Ch the window, +10h
 *       a dword whose low word is wParam, +14h the message; the first two
 *       dwords SHELL's own) that the following calls hand back in EDI:
 *       414h a hot key in a full-screen VM (wParam 1: Alt+Enter), 41Ah the
 *       VM's display changed (windowed; +12h 0 or 20h), 418h the VM's
 *       program has ended;
 *   0Ch an event record handed back; 16h EDX 0; 4 destroy the VM EBX
 *       (Destroy_VM); 0Ah nothing seen.
 * When the VM's program ends: VM_Terminate, VM_Suspend,
 * VM_Not_Executeable, the three devices' focus and the execution focus
 * back to the System VM, then event 418h; WINOA386 then gives the System
 * VM the focus (5), calls 6, 16h, 0Ch and destroys the VM (4).
 * UNVERIFIED: the meaning of the other block fields and registers (PIF
 * settings: EAX 1902h, EBX F0h, ECX 207CFh at function 3), and what
 * Windows 3.1 returns for failures. Sources for the names: the Windows
 * 3.1 DDK's Virtual Device Adaptation Guide (.tools/docs/vdag31: the
 * control messages, Set_Device_Focus's ESI flags and EDI, the VMM
 * services named above).
 */
#include "vmm.h"

#define SHELL_WINDOWED  0x0001          /* function 6's bit 0 */
extern int dpmi_verbose;

/* SHELL's view of a DOS VM */
struct shvm {
    struct shvm *next;
    struct vm *vm;
    u32 block;                          /* WINOA386's block (function 3), linear */
    u32 flags;                          /* function 6 */
    u32 flags2;                         /* function 6 with CX FFFFh */
    u32 ended_posted;
    int hk_pending;                     /* a hot key's event not acted on (function 5) */
    u32 hwnd;                           /* function 15h */
    u32 cb_sel, cb_off;                 /* function 9: its event procedure */
};
static struct shvm *shvms;
static u32 next_sel, next_off;          /* function 9's procedure, for the next VM */

/* event records WINOA386 reads through the flat selector (ring 3: their
 * page is made user-accessible), as Windows 3.1's (above). UNVERIFIED:
 * what WINOA386 reads of them */
#pragma pack(1)
struct shev { u32 own1, own2, vm; u16 hwnd, zero; u32 data; u16 msg; u8 rest[0x48 - 0x16]; };
#pragma pack()
#define NEVENTS 32
static struct shev *evs;
static u32 ev_next;

static int is_command(const char *p);
static void put_banner(struct vm *v);
static void post(struct vm *v, u32 msg, u32 wparam);
static void focus3(struct vm *v, u32 edi, int all);

static struct shvm *sh_of(struct vm *v)
{
    struct shvm *s;
    for (s = shvms; s; s = s->next)
        if (s->vm == v) return s;
    return NULL;
}

/* SHELL's hot keys, active in the DOS VMs only (VKD_Local_Disable_Hot_Key
 * for the System VM: Windows has these keys itself). A hot key in a DOS
 * VM becomes event 414h for its WINOA386, wParam the key's number
 * (Windows 3.1: 1 for Alt+Enter). VKD's shift bits are w16's (vkd.c):
 * Shift 1, Ctrl 2, Alt 4; the mask in the high word */
extern char shell_hk_entry[];
#pragma aux shell_hk_entry "_*"
struct hk { u8 scan; u32 shift; u32 code; };
#define HK_ALTENTER 1
#define HK_ALTTAB   2
#define HK_ALTESC   3
#define HK_CTRLESC  4
static const struct hk hks[] = {
    { 0x1C, 0x00070004, HK_ALTENTER },
    { 0x0F, 0x00070004, HK_ALTTAB },
    { 0x01, 0x00070004, HK_ALTESC },
    { 0x01, 0x00070002, HK_CTRLESC },
};
static u32 switch_key;                  /* the switching key being acted on (function 10h) */
static u32 switch_wnd;                  /* function 10h's first ECX */

static void define_hot_keys(void)
{
    struct svcregs s;
    u32 i;
    for (i = 0; i < sizeof(hks) / sizeof(hks[0]); i++) {
        memset(&s, 0, sizeof(s));
        s.eax = hks[i].scan | (0xFF << 8);      /* normal or extended */
        s.ebx = hks[i].shift;
        s.ecx = 1;                              /* on the press */
        s.esi = (u32)shell_hk_entry;
        s.edx = hks[i].code;
        if (!vxd_service_call(0x000D, 0x01, &s) || (s.eflags & EFL_CF)) continue;
        s.ebx = VMH(&sysvm);
        vxd_service_call(0x000D, 0x04, &s);     /* VKD_Local_Disable_Hot_Key: EAX the key */
    }
}

/* the hot key's callback (VKD: EAX the scan code, EDX our number), in
 * whatever VM is running: for the VM with the keyboard */
void __cdecl shell_hot_key(struct svcregs *r)
{
    struct vm *v = vm_focus;
    r->eflags &= ~EFL_CF;
    if (v == &sysvm || !sh_of(v)) return;
    trace("SHELL: hot key %d in VM %d\n", r->edx, v->id);
    if (sh_of(v)->hk_pending) return;   /* the key repeated */
    sh_of(v)->hk_pending = 1;
    if (r->edx == HK_ALTENTER) {
        post(v, 0x414, 0x00010000u | r->edx);
        return;
    }
    /* a switching key in a DOS VM (Windows 3.1, VXSH: Alt+Tab, then Alt+Esc
     * in the full-screen MS-DOS Prompt): Alt+Tab begins message mode for
     * the VM (control message 10h, ECX 1), both give the System VM the
     * execution focus, suspend the VM (VM_Suspend, EDX 0 after Alt+Tab, 1
     * after Alt+Esc) and post event 41Eh, wParam 0, for its WINOA386, which
     * then asks function 10h which key it was. Ctrl+Esc: 3.1 posted no
     * event that VXSH saw (WINOA386 gave the System VM the focus itself,
     * function 5): UNVERIFIED, here the same event as Alt+Esc */
    if (r->edx == HK_CTRLESC) {
        /* Ctrl+Esc (Windows 3.1: the Task List over Windows, no event
         * VXSH saw; WINOA386 then gave the System VM the focus with
         * function 5, EAX 1): the System VM gets the devices' focus and
         * the keys, Ctrl down and the Esc press (VKD_Force_Keys,
         * VKD_Reflect_Hot_Key), so USER opens its Task List and WINOA386
         * is deactivated. UNVERIFIED how Windows 3.1 does it */
        static const u8 ctrl_down = 0x1D;
        struct svcregs k;
        struct shvm *s = sh_of(v);
        u32 hk = r->ebx;
        focus3(&sysvm, 0, 1);
        vm_set_focus(&sysvm);
        if (!(s->flags & SHELL_WINDOWED) && !v->suspended) {
            v->suspended = 1;
            vxd_send(VM_SUSPEND, v, 1, 0, 0);
        }
        memset(&k, 0, sizeof(k));
        k.esi = (u32)&ctrl_down;
        k.ecx = 1;
        vxd_service_call(0x000D, 0x07, &k);     /* VKD_Force_Keys */
        memset(&k, 0, sizeof(k));
        k.eax = hk;
        k.ebx = VMH(&sysvm);
        k.ecx = 2;                              /* Ctrl (w16's SS_CTRL) */
        vxd_service_call(0x000D, 0x05, &k);     /* VKD_Reflect_Hot_Key */
        s->hk_pending = 0;
        return;
    }
    switch_key = r->edx;
    if (r->edx == HK_ALTTAB) vxd_send(BEGIN_MESSAGE_MODE, v, 0, 0, 0);
    vm_set_focus(&sysvm);
    {
        struct shvm *s = sh_of(v);
        if (!(s->flags & SHELL_WINDOWED) && !v->suspended) {
            v->suspended = 1;
            vxd_send(VM_SUSPEND, v, r->edx == HK_ALTTAB ? 0 : 1, 0, 0);
        }
    }
    post(v, 0x41E, 0);
}

void shell_init(void)
{
    evs = kmalloc(NEVENTS * sizeof(*evs));       /* more than a page: page aligned */
    if (evs) make_user((u32)evs, NEVENTS * sizeof(*evs));
    define_hot_keys();
}

/* the VDD: a windowed VM's screen has changed (event 41Ah; the record's
 * +12h: 20h when the cursor moved, as Windows 3.1's varied 0 and 20h) */
void shell_display_changed(struct vm *v, int cursor)
{
    if (sh_of(v)) post(v, 0x41A, cursor ? 0x00200000u : 0);
}

/* SHELL's event: WINOA386's procedure in the System VM, (hwnd, msg,
 * wParam, lParam = the record) */
struct postarg { u32 msg, wparam; struct vm *vm; };

static void post_now(struct vm *sys, void *arg)
{
    struct postarg *a = arg;
    struct shvm *s = sh_of(a->vm);
    struct shev *e;
    u16 args[5];
    (void)sys;
    if (!s || !s->cb_sel || !evs) { kfree(a); return; }
    e = &evs[ev_next++ % NEVENTS];
    memset(e, 0, sizeof(*e));
    e->vm = VMH(a->vm);
    e->hwnd = (u16)s->hwnd;
    e->data = a->wparam;
    e->msg = (u16)a->msg;
    args[0] = (u16)s->hwnd;
    args[1] = (u16)a->msg;
    args[2] = (u16)a->wparam;
    args[3] = (u16)((u32)e >> 16);
    args[4] = (u16)(u32)e;
    trace("SHELL: event %x (%x) for VM %d to %x:%x\n", a->msg, a->wparam,
          a->vm->id, s->cb_sel, s->cb_off);
    dpmi_pm_farcall(s->cb_sel, s->cb_off, args, 5, NULL);
    kfree(a);
}

static void post(struct vm *v, u32 msg, u32 wparam)
{
    struct postarg *a = kmalloc(sizeof(*a));
    if (!a) return;
    a->msg = msg;
    a->wparam = wparam;
    a->vm = v;
    vm_event(&sysvm, post_now, a);
}

static void focus3(struct vm *v, u32 edi, int all)
{
    vxd_send(SET_DEVICE_FOCUS, v, 0x000D, 0x0F20, edi);
    vxd_send(SET_DEVICE_FOCUS, all ? v : &sysvm, 0x000A, 0x0F20, edi);
    vxd_send(SET_DEVICE_FOCUS, all ? v : &sysvm, 0x000C, 0x0F20, edi);
}

static void resume_vm(struct vm *v)
{
    if (!v->suspended || v->ended) return;
    v->suspended = 0;
    vxd_send(VM_RESUME, v, 0, 0, 0);
    vm_wake(v);
}

static void suspend_vm(struct vm *v)
{
    if (v->suspended || v == &sysvm) return;
    v->suspended = 1;
    vxd_send(VM_SUSPEND, v, 0, 0, 0);
}

/* function 5: the focus to v */
static void set_focus(struct vm *v, u32 edi)
{
    struct vm *old = vm_focus;
    struct shvm *s = sh_of(v);
    int windowed = s && (s->flags & SHELL_WINDOWED);
    focus3(v, edi, v == &sysvm || !windowed);
    vm_set_focus(v);
    if (v != &sysvm) resume_vm(v);
    else if (old != &sysvm && (s = sh_of(old)) != NULL && !(s->flags & SHELL_WINDOWED))
        suspend_vm(old);                /* a full-screen VM in the background: stopped */
}

/* the 48-bit pointer at p (offset, selector) as a linear address, in place */
static u32 fptr_lin(u8 *p)
{
    u32 off = *(u32 *)p, sel = *(u16 *)(p + 4), lin = sel_base(sel) + (off & 0xFFFF);
    *(u32 *)p = lin;
    return lin;
}

static void create(struct cregs *r)
{
    u8 *b = (u8 *)pm_lin(r->es, r->edi);
    char *prog, *dir;
    u8 *tail;
    struct vm *v;
    struct shvm *s;

    prog = (char *)fptr_lin(b + 0x08);
    tail = (u8 *)fptr_lin(b + 0x0E);
    dir = (char *)fptr_lin(b + 0x14);
    trace("SHELL: create a VM for %s, directory %s\n", prog, dir);
    v = vm_create();
    s = kmalloc(sizeof(*s));
    if (!v || !s) { r->eax = 0; r->eflags |= EFL_CF; return; }
    s->vm = v;
    s->block = (u32)b;
    s->flags = 0;
    s->flags2 = 0;
    s->ended_posted = 0;
    s->hwnd = 0;
    s->cb_sel = next_sel;
    s->cb_off = next_off;
    s->next = shvms;
    shvms = s;
    v->prio_fg = *(u16 *)(b + 0x1E) ? *(u16 *)(b + 0x1E) : 100;
    v->prio_bg = *(u16 *)(b + 0x20) ? *(u16 *)(b + 0x20) : 50;
    vm_set_program(v, prog, tail, dir);
    if (is_command(prog)) put_banner(v);
    if (vxd_send(CREATE_VM, v, 0, (u32)b, r->edi)) {
        trace("SHELL: a device refused the VM\n");
        vxd_send_rev(DESTROY_VM, v, 0, 0, 0);
        vm_destroy(v);
        r->eax = 0;
        r->eflags |= EFL_CF;
        return;
    }
    r->eax = VMH(v);
}

/* The box a DOS VM writes to standard output before it runs COMMAND.COM
 * (VXS.386's INT 21h hook under Windows 3.1: for the MS-DOS Prompt and
 * for File > Run COMMAND.COM alike, so it goes with the program, not the
 * PIF; UNVERIFIED for other names of the shell): CR LF, then each line
 * indented 7 columns in a double-line box (code page 437) 64 columns wide
 * inside; "w16" where Windows 3.1 has "Windows" (ARCHITECTURE.md 3) */
static const char *const banner[] = {
    "  \xFE Type EXIT and press ENTER to quit this MS-DOS prompt and",
    "    return to w16.",
    "  \xFE Press ALT+TAB to switch to w16 or another application.",
    "  \xFE Press ALT+ENTER to switch this MS-DOS Prompt between a",
    "    window and full screen.",
};
#define BANNER_W 64

static int is_command(const char *p)
{
    const char *n = p, *c = "COMMAND.COM";
    for (; *p; p++) if (*p == '\\' || *p == ':') n = p + 1;
    for (; *n && *c; n++, c++)
        if ((*n >= 'a' && *n <= 'z' ? *n - 32 : *n) != *c) return 0;
    return !*n && !*c;
}

static void put_banner(struct vm *v)
{
    char buf[(BANNER_W + 12) * 8];
    u32 n = 0, i, j, k;
    buf[n++] = '\r'; buf[n++] = '\n';
    for (i = 0; i < sizeof(banner) / sizeof(banner[0]) + 2; i++) {
        for (j = 0; j < 7; j++) buf[n++] = ' ';
        if (i == 0 || i == sizeof(banner) / sizeof(banner[0]) + 1) {
            buf[n++] = (char)(i ? 0xC8 : 0xC9);
            for (j = 0; j < BANNER_W; j++) buf[n++] = (char)0xCD;
            buf[n++] = (char)(i ? 0xBC : 0xBB);
        } else {
            const char *t = banner[i - 1];
            buf[n++] = (char)0xBA;
            for (j = 0; t[j]; j++) buf[n++] = t[j];
            for (k = j; k < BANNER_W; k++) buf[n++] = ' ';
            buf[n++] = (char)0xBA;
        }
        buf[n++] = '\r'; buf[n++] = '\n';
    }
    vm_set_message(v, buf, n);
}

static void destroy(struct vm *v)
{
    struct shvm **pp, *s;
    if (!v || v == &sysvm) return;
    if (!v->ended) {
        /* closing a running VM: UNVERIFIED (Windows 3.1's "application
         * still active" handling comes first) */
        v->ended = 1;
        suspend_vm(v);
        vxd_send(VM_NOT_EXECUTEABLE, v, 0, 0, 0);
        if (vm_focus == v) { focus3(&sysvm, 0, 1); vm_set_focus(&sysvm); }
    }
    vxd_send(DESTROY_VM, v, 0, 0x0F20, 0);
    vdd_vm_destroy(v);
    for (pp = &shvms; (s = *pp) != NULL; pp = &s->next)
        if (s->vm == v) { *pp = s->next; kfree(s); break; }
    vm_destroy(v);
}

void shell_api(struct cregs *r)
{
    u32 fn = DX(r);
    struct vm *v;
    struct shvm *s;
    r->eflags &= ~EFL_CF;
    if (fn != 0x0C && dpmi_verbose)
        trace("SHELL: %x eax %x ebx %x ecx %x esi %x edi %x\n", fn, r->eax, r->ebx, r->ecx, r->esi, r->edi);
    switch (fn) {
    case 0x00:                          /* the version */
        SETW(r->eax, 0x030A);
        r->ebx = VMH(&sysvm);
        return;
    case 0x09:                          /* the event procedure; the flat selector */
        {
            u8 *b = (u8 *)pm_lin(r->es, r->edi);
            next_off = *(u32 *)(b + 8);
            next_sel = *(u16 *)(b + 12);
            if (dpmi_verbose) {
                u32 i;
                trace("SHELL: 9's block");
                for (i = 0; i < 48; i += 4) trace(" %x", *(u32 *)(b + i));
                trace("\n");
            }
        }
        SETW(r->edx, SEL_FLAT7B);
        /* EBX and ECX come back 0 whatever came in (Windows 3.1, VXS.386:
         * in 78h and 1D20h, left over from w16's VGA.3GR, out 0 and 0).
         * Left nonzero, WINOA386 refused every DOS program: "You cannot
         * start a non-Windows application because the x TSR is loaded"
         * (UNVERIFIED what the pair names when it is not 0: a TSR Windows
         * found, by the message) */
        r->eax = 0;
        r->ebx = 0;
        r->ecx = 0;
        trace("SHELL: event procedure %x:%x\n", next_sel, next_off);
        return;
    case 0x03:
        if (dpmi_verbose) {
            u8 *b = (u8 *)pm_lin(r->es, r->edi);
            u32 i;
            trace("SHELL: 3's block");
            for (i = 0; i < 0x50; i += 4) trace(" %x", *(u32 *)(b + i));
            trace("\n");
        }
        create(r);
        return;
    case 0x05:                          /* the focus */
        if ((v = vm_of(r->ebx)) == NULL) { r->eflags |= EFL_CF; return; }
        for (s = shvms; s; s = s->next) s->hk_pending = 0;
        set_focus(v, r->edi);
        r->eax = 0;
        return;
    case 0x06:                          /* the VM's flags */
        /* WINOA386 calls it twice at each change, the second time with
         * CX FFFFh. Observed (VXS.386 under Windows 3.1 and w16's trace,
         * Microsoft's WINOA386): to a window (AX 1, CX 07B7h), (AX 1, CX
         * FFFFh); back to full screen with DOSPRMPT.PIF (AX 0, CX 1), (AX
         * 10h, CX FFFFh), and for COMMAND.COM without a PIF (AX 0, CX 0),
         * (AX 1, CX FFFFh), which Windows 3.1's SHELL also took for full
         * screen. So the first call's AX is the flags (bit 0 windowed);
         * the CX FFFFh call is something else, kept aside (UNVERIFIED:
         * what CX and that second word mean; ECX's high word is left
         * over). w16 read (AX & CX) as the bits to set: COMMAND.COM's
         * window never went back to full screen (it became an icon) */
        if ((s = sh_of(vm_of(r->ebx))) != NULL) {
            u32 old = s->flags;
            if (CX(r) == 0xFFFF) {
                s->flags2 = AX(r);
                return;
            }
            s->flags = AX(r);
            if ((old ^ s->flags) & SHELL_WINDOWED)
                vdd_window(s->vm, (s->flags & SHELL_WINDOWED) != 0);
        }
        return;
    case 0x07:                          /* the block's status word */
        if ((s = sh_of(vm_of(r->ebx))) != NULL)
            *(u16 *)s->block = (u16)((*(u16 *)s->block & ~0x000Fu) | (AX(r) & 0x000F));
        return;
    case 0x15:                          /* WINOA386's window, for the VM just created */
        for (s = shvms; s && s->hwnd; s = s->next)
            ;
        if (s) s->hwnd = SI(r);
        r->eax = 0;
        trace("SHELL: VM %d's window %x\n", s ? s->vm->id : 0, SI(r));
        return;
    case 0x0C:                          /* an event record back */
        /* EAX 1 (after a switching key, Windows 3.1): the devices' focus
         * and the execution focus to the System VM, EAX 1 back */
        if (AX(r) == 1) {
            focus3(&sysvm, 0, 1);
            vm_set_focus(&sysvm);
            r->eax = 1;
        } else
            r->eax = 0;
        return;
    case 0x10:                          /* after event 41Eh: which key */
        /* Windows 3.1 (VXSH): EBX FFFFFFFFh first: Alt+Tab EAX 1, EDX 0
         * (WINOA386 calls again with EBX the VM: the devices' focus to the
         * System VM, message mode ended (control 11h), EAX 0, EDX 1);
         * Alt+Esc EAX 0, EDX 0, the devices' focus to the System VM.
         * Ctrl+Esc: UNVERIFIED, as Alt+Esc */
        if (r->ebx == 0xFFFFFFFFu) switch_wnd = CX(r);
        if (r->ebx == 0xFFFFFFFFu && switch_key == HK_ALTTAB) {
            r->eax = 1;
            SETW(r->edx, 0);
            return;
        }
        focus3(&sysvm, r->edi, 1);
        if (r->ebx != 0xFFFFFFFFu && (v = vm_of(r->ebx)) != NULL) {
            /* back: EBX FFFFFFFFh, ECX the window the first call named
             * (Windows 3.1: 1168h both times; WINOA386 then activates
             * it with function 0Ch) */
            vxd_send(END_MESSAGE_MODE, v, 0, 0, 0);
            r->eax = 0;
            r->ebx = 0xFFFFFFFFu;
            SETW(r->ecx, switch_wnd);
            SETW(r->edx, 1);
        } else {
            r->eax = 0;
            SETW(r->edx, 0);
        }
        switch_key = 0;
        return;
    case 0x14:                          /* (seen after a switch: nothing back) */
        return;
    case 0x16:
        SETW(r->edx, 0);
        return;
    case 0x04:                          /* destroy the VM */
        destroy(vm_of(r->ebx));
        return;
    case 0x0A:
        return;
    }
    trace("SHELL: function %x (ax=%x bx=%x cx=%x si=%x di=%x) not implemented\n",
          fn, AX(r), r->ebx, CX(r), SI(r), DI(r));
}

/* the VM's program has ended (vm.c, in that VM): Windows 3.1's sequence,
 * then the event for WINOA386 */
void shell_vm_ended(struct vm *v)
{
    struct shvm *s = sh_of(v);
    vxd_send(VM_TERMINATE, v, 0, 0, 0);
    suspend_vm(v);
    vxd_send(VM_NOT_EXECUTEABLE, v, 0, 0, 0);
    if (vm_focus == v) {
        focus3(&sysvm, 0, 1);
        vm_set_focus(&sysvm);
    }
    if (s && !s->ended_posted) {
        s->ended_posted = 1;
        post(v, 0x418, 0);
    }
}
