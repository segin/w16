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
    int idle_detect;                    /* the PIF's Detect Idle Time (+01h bit 10h) */
    u32 flags;                          /* function 6 */
    u32 flags2;                         /* function 6 with CX FFFFh */
    u32 ended_posted;
    int hk_pending;                     /* a hot key's event not acted on (function 5) */
    char title[32];                     /* function 3's block +2Eh: the window's title */
    u32 hwnd;                           /* function 15h */
    u16 caller_ds;                      /* function 3's ES: the creating WINOA386's data segment */
    u32 cb_sel, cb_off;                 /* function 9: its event procedure */
    int pif_excl;                       /* the block's +00h bit 01h: Exclusive */
};

/* the PIF's Exclusive holds while the VM is full screen (sh_mode) */
static void sh_mode(struct shvm *s)
{
    s->vm->exclusive = s->pif_excl && !(s->flags & SHELL_WINDOWED);
}
static struct shvm *shvms;
static int last_fault;                  /* the last event posted was a fault's (41Bh) */
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
/* Alt+Tab held in a full-screen VM: function 10h's titles (below) */
static struct vm *sw_vm;                /* the VM it was pressed in */
static u32 sw_calls;                    /* function 10h calls so far */
static int sw_wait;                     /* a call waits (made again: vxdapi_again) */
static u32 sw_tabs, sw_base;            /* Tab presses (the hot key again), at the call */
static int sw_esc;                      /* Esc pressed (Alt+Esc's hot key) */
static u32 sw_ebx, sw_ecx;              /* the window shown */
static u16 sw_screen[80 * 25];          /* the VM's text screen under the title */
static int sw_saved;
static u8 sw_cursor;                    /* CRTC 0Ah as it was */
static int sw_to_windows;               /* let go on a Windows window (function 0Ch) */

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
    /* Tab again while the title shows, or Esc (function 10h) */
    if (sw_vm && switch_key == HK_ALTTAB) {
        if (r->edx == HK_ALTTAB) sw_tabs++;
        else if (r->edx == HK_ALTESC) sw_esc = 1;
        return;
    }
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
    if ((sh_of(v)->flags & SHELL_WINDOWED) && r->edx == HK_ALTESC) {
        /* Alt+Esc in a window: nothing (Windows 3.1: the MS-DOS Prompt's
         * window stayed active, the screen as it was; UNVERIFIED whether
         * the program in it gets the keys) */
        sh_of(v)->hk_pending = 0;
        return;
    }
    if (sh_of(v)->flags & SHELL_WINDOWED) {
        /* in a window (Windows 3.1: Alt+Tab from the MS-DOS Prompt's
         * window showed USER's switch box over Windows; let go, Program
         * Manager active): the keys are Windows', as Ctrl+Esc's above -
         * the System VM gets the keyboard, Alt down and the key's press,
         * and USER switches. UNVERIFIED how Windows 3.1 does it */
        static const u8 alt_down = 0x38;
        struct svcregs k;
        struct shvm *s = sh_of(v);
        u32 hk = r->ebx;
        focus3(&sysvm, 0, 1);
        vm_set_focus(&sysvm);
        memset(&k, 0, sizeof(k));
        k.esi = (u32)&alt_down;
        k.ecx = 1;
        vxd_service_call(0x000D, 0x07, &k);     /* VKD_Force_Keys */
        memset(&k, 0, sizeof(k));
        k.eax = hk;
        k.ebx = VMH(&sysvm);
        k.ecx = 4;                              /* Alt (w16's SS_ALT) */
        vxd_service_call(0x000D, 0x05, &k);     /* VKD_Reflect_Hot_Key */
        s->hk_pending = 0;
        return;
    }
    switch_key = r->edx;
    if (r->edx == HK_ALTTAB) {
        vxd_send(BEGIN_MESSAGE_MODE, v, 0, 0, 0);
        sw_vm = v;
        sw_calls = sw_tabs = 0;
        sw_wait = sw_esc = 0;
        sw_to_windows = 0;
    }
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
int shell_display_changed(struct vm *v, int cursor)
{
    /* (not before WINOA386 has its window for the VM, function 15h, and
     * the VM runs: a VM windowed from its start changed nothing yet) */
    struct shvm *s = sh_of(v);
    if (!s || !s->hwnd || !v->started) return 0;
    post(v, 0x41A, cursor ? 0x00200000u : 0);
    return 1;
}

/* SHELL's event: WINOA386's procedure in the System VM, (hwnd, msg,
 * wParam, lParam = the record) */
struct postarg { u32 msg, wparam; struct vm *vm; u32 gen; u32 data; };

static void post_now(struct vm *sys, void *arg)
{
    struct postarg *a = arg;
    /* the VM it was for, not another made since in its slot */
    struct shvm *s = a->vm->gen == a->gen && a->vm->id ? sh_of(a->vm) : NULL;
    struct shev *e;
    u16 args[5];
    (void)sys;
    if (!s || !s->cb_sel || !evs) { kfree(a); return; }
    e = &evs[ev_next++ % NEVENTS];
    memset(e, 0, sizeof(*e));
    e->vm = VMH(a->vm);
    e->hwnd = (u16)s->hwnd;
    e->data = a->data;
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
    last_fault = msg == 0x41B;
    if (!a) return;
    a->msg = msg;
    a->wparam = wparam;
    a->data = wparam;
    a->vm = v;
    a->gen = v->gen;
    vm_event(&sysvm, post_now, a);
}

/* SHELL_Message (SHELL service 4: EBX the VM, EAX the MB_ flags, ECX the
 * text, EDI the caption, ESI a callback with EDX): a DOS VM's message goes
 * to its WINOA386 as w16's event SHELL_EV_MESSAGE (wParam the flags, the
 * event record's data the text's linear address), which shows it in a
 * message box with the VM's title (Windows 3.1: VSBD.386's contention
 * message came so, captioned with the box's title, the box inactive behind
 * it: ARCHITECTURE.md 24.6). UNVERIFIED: the event Windows 3.1's SHELL
 * sends for it (w16's WINOA386 has this one; Microsoft's ignores it), a
 * caption given in EDI (not shown), the System VM's messages (carry: none
 * shown), the callback (called at once with IDOK) */
#define SHELL_EV_MESSAGE 0x4F0
#define NMSG 4                          /* texts WINOA386 can read (ring 3) */
static char (*msgbuf)[256];
static u32 msg_next;
static int shell_message(struct svcregs *r)
{
    char *t;
    u32 i;
    struct vm *v = vm_of(r->ebx);
    struct shvm *s = v ? sh_of(v) : NULL;
    struct postarg *a;
    trace("SHELL: SHELL_Message for VM %d: %x \"%s\"\n", v ? v->id : 0, r->eax, (const char *)r->ecx);
    if (!msgbuf && (msgbuf = kmalloc(NMSG * 256 + 4096)) != NULL) {
        msgbuf = (char (*)[256])(((u32)msgbuf + 4095) & ~4095u);
        make_user((u32)msgbuf, NMSG * 256);
    }
    if (!s || !s->cb_sel || !msgbuf || !(a = kmalloc(sizeof(*a)))) { r->eflags |= EFL_CF; return 1; }
    /* the text where the VM's WINOA386 can read it (the device's own
     * memory is ring 0's) */
    t = msgbuf[msg_next++ % NMSG];
    for (i = 0; i < 255 && ((const char *)r->ecx)[i]; i++) t[i] = ((const char *)r->ecx)[i];
    t[i] = 0;
    a->msg = SHELL_EV_MESSAGE;
    a->wparam = r->eax & 0xFFFF;
    a->data = (u32)t;
    a->vm = v;
    a->gen = v->gen;
    vm_event(&sysvm, post_now, a);
    r->eax = (u32)a;
    if (r->esi) {
        struct svcregs c;
        memset(&c, 0, sizeof(c));
        c.eax = 1;
        c.edx = r->edx;
        vmm_call_proc(r->esi, &c);
    }
    return 1;
}

/* the SHELL device's services (device 0017h) for other VxDs */
int shell_service(u32 svc, struct svcregs *r)
{
    switch (svc) {
    case 0x0000:                        /* SHELL_Get_Version (UNVERIFIED value) */
        r->eax = (r->eax & 0xFFFF0000u) | 0x030A;
        return 1;
    case 0x0004:
        return shell_message(r);
    case 0x0005: {                      /* SHELL_GetVMInfo: EBX the VM, EAX its flags */
        /* the DDK: the PIF's settings as SGVMI_ flags (names only; the
         * values not given). SGVMI_Windowed is 04h (Windows 3.1:
         * 40021004h for a windowed box, 40021000h for the same PIF full
         * screen; VMTASK.386, tests/vmtaskrun.sh, ARCHITECTURE.md 15.12);
         * here the VM's window state now. UNVERIFIED and left 0: the
         * other flags (reserved keys, polling, locks, close on exit),
         * and whether 3.1's bit follows Alt+Enter */
        struct shvm *s = sh_of(vm_of(r->ebx));
        r->eax = s && (s->flags & SHELL_WINDOWED) ? 0x04 : 0;
        return 1;
    }
    }
    return 0;
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
        suspend_vm(old);                /* a full-screen VM in the background: stopped
                                         * (also with its PIF's Background: its video
                                         * memory is the VGA's, not RAM, while it is
                                         * full screen; left to do) */
}

/* the 48-bit pointer at p (offset, selector) as a linear address, in
 * place; 0 when the client could not read n bytes there */
static u32 fptr_lin(u8 *p, u32 n)
{
    u32 off = *(u32 *)p, sel = *(u16 *)(p + 4);
    u8 *lin = cl_ptr(sel, off & 0xFFFF, n, 0);
    *(u32 *)p = (u32)lin;
    return (u32)lin;
}

static void create(struct cregs *r)
{
    u8 *b = cl_ptr(r->es, r->edi, 0x22, 1);
    char *prog, *dir;
    u8 *tail;
    struct vm *v;
    struct shvm *s;

    /* (WINOA386's block and its strings; a pointer the VMM cannot
     * follow fails the call: UNVERIFIED what Windows 3.1 does) */
    if (!b) { r->eax = 0; r->eflags |= EFL_CF; return; }
    prog = (char *)fptr_lin(b + 0x08, 0x80);
    tail = (u8 *)fptr_lin(b + 0x0E, 0x80);
    dir = (char *)fptr_lin(b + 0x14, 0x80);
    if (!prog || !tail || !dir) { r->eax = 0; r->eflags |= EFL_CF; return; }
    trace("SHELL: create a VM for %s, directory %s\n", prog, dir);
    v = vm_create();
    s = v ? kmalloc(sizeof(*s)) : NULL;
    if (!v || !s) {
        if (v) vm_destroy(v);
        r->eax = 0;
        r->eflags |= EFL_CF;
        return;
    }
    s->vm = v;
    s->block = (u32)b;
    s->caller_ds = (u16)r->es;
    /* a VM that starts in a window: the block's first byte, bit 2
     * (Windows 3.1, VXS.386: 04h for a PIF that starts windowed, 00h for
     * DOSPRMPT.PIF; the same 4 as function 7's status word); its video
     * memory is RAM from the start (vdd.c) */
    s->flags = (b[0] & 4) ? SHELL_WINDOWED : 0;
    if (s->flags & SHELL_WINDOWED) vdd_window(v, 1);
    s->flags2 = 0;
    s->ended_posted = 0;
    s->hwnd = 0;
    s->cb_sel = next_sel;
    s->cb_off = next_off;
    /* the title (Windows 3.1's block: "MS-DOS Prompt" at +2Eh, VXS.386) */
    {
        u32 i;
        for (i = 0; i < sizeof(s->title) - 1 && b[0x2E + i]; i++) s->title[i] = b[0x2E + i];
        s->title[i] = 0;
    }
    s->next = shvms;
    shvms = s;
    v->prio_fg = *(u16 *)(b + 0x1E) ? *(u16 *)(b + 0x1E) : 100;
    v->prio_bg = *(u16 *)(b + 0x20) ? *(u16 *)(b + 0x20) : 50;
    /* the rest of the PIF (WINOA386's block as Windows 3.1's fills it,
     * tests/pifread.sh): +00h bit 01h Exclusive, 02h Background, +1Ah KB
     * Desired (the VM's conventional memory, vm_set_memory); Background
     * is kept (v->background) but not acted on yet (set_focus). UNVERIFIED
     * and not acted on: +01h-+03h (idle time, HMA, locked memory, fast
     * paste, the reserved keys; +03h 40h close on exit is WINOA386's),
     * +04h the video settings, +1Ch KB Required, +22h-+28h EMS and XMS */
    s->idle_detect = (b[1] & 0x10) != 0;
    s->pif_excl = (b[0] & 1) != 0;
    v->background = (b[0] & 2) != 0;
    sh_mode(s);
    vm_set_program(v, prog, tail, dir);
    vm_set_memory(v, *(u16 *)(b + 0x1A));
    v86mm_vm_create(v, b);              /* its XMS and EMS: the PIF's limits */
    if (is_command(prog)) put_banner(v);
    if (vxd_send(CREATE_VM, v, 0, (u32)b, r->edi)) {
        struct shvm **pp;
        trace("SHELL: a device refused the VM\n");
        vxd_send_rev(DESTROY_VM, v, 0, 0, 0);
        for (pp = &shvms; *pp != s; pp = &(*pp)->next)
            ;
        *pp = s->next;
        kfree(s);
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
        /* a running VM: its Settings dialog's Terminate (Windows 3.1,
         * VXSH.386: function 4 with EAX 1; the three devices' focus and
         * the execution focus to the System VM, VM_Suspend,
         * VM_Not_Executeable with EDX 2, then Destroy_VM) */
        v->ended = 1;
        if (vm_focus == v) { focus3(&sysvm, 0, 1); vm_set_focus(&sysvm); }
        suspend_vm(v);
        vxd_send(VM_NOT_EXECUTEABLE, v, 2, 0, 0);
    }
    vxd_send(DESTROY_VM, v, 0, 0x0F20, 0);
    vdd_vm_destroy(v);
    for (pp = &shvms; (s = *pp) != NULL; pp = &s->next)
        if (s->vm == v) { *pp = s->next; kfree(s); break; }
    vm_destroy(v);
}

/* Alt+Tab held in a full-screen VM (Windows 3.1, VXSH and screens,
 * tests/scripts/input/alttabvxs.txt and alttab386.txt): WINOA386 offers
 * the windows in turn with function 10h (EBX FFFFFFFFh and the window in
 * ECX for a Windows one, EBX the VM for its own; ES:DI the title). The
 * first call shows its title and answers EAX 1 at once; each later call
 * waits, the last title still shown, until Tab is pressed again (its
 * title shown, EAX 1) or Alt let go: EAX 0, EBX and ECX the window
 * shown, EDX 1, the devices' focus first to its VM (the System VM for a
 * Windows window, the VM itself for its own title) and message mode
 * ended. Esc pressed answers as Alt let go (3.1: Program Manager's title
 * shown, Esc: EBX FFFFFFFFh, ECX its window, Windows came up). The
 * screen: 80 x 25 text, light gray, the title white on blue in row 1,
 * columns 1-78, from column (81 - length) / 2 ("Program Manager" from 33,
 * "MS-DOS Prompt" from 34), no cursor. w16 shows it in the VGA's text
 * memory, the VM's screen kept and put back before the focus moves */
static void sw_show(struct cregs *r)
{
    u16 *t = (u16 *)phys_to_lin(0xB8000);
    const char *p = (const char *)cl_ptr(r->es, r->edi, 1, 0) ? (const char *)pm_lin(r->es, r->edi) : "";
    u32 i, n, x;
    if (!sw_saved) {
        for (i = 0; i < 80 * 25; i++) sw_screen[i] = t[i];
        outb(0x3D4, 0x0A);
        sw_cursor = (u8)inb(0x3D5);
        sw_saved = 1;
    }
    outb(0x3D4, 0x0A);
    outb(0x3D5, sw_cursor | 0x20);          /* no cursor */
    for (i = 0; i < 80 * 25; i++) t[i] = 0x7020;
    for (i = 1; i <= 78; i++) t[80 + i] = 0x1F20;
    for (n = 0; n < 78 && p[n]; n++)
        ;
    x = (81 - n) / 2;
    for (i = 0; i < n; i++) t[80 + x + i] = 0x1F00 | (u8)p[i];
    sw_ebx = r->ebx;
    sw_ecx = CX(r);
}

static void sw_restore(void)
{
    u16 *t = (u16 *)phys_to_lin(0xB8000);
    u32 i;
    if (!sw_saved) return;
    for (i = 0; i < 80 * 25; i++) t[i] = sw_screen[i];
    outb(0x3D4, 0x0A);
    outb(0x3D5, sw_cursor);
    sw_saved = 0;
}

static void alt_tab_title(struct cregs *r)
{
    struct vm *v = sw_vm;
    int up = 0;
    if (!sw_wait) {
        if (sw_calls++ == 0) {
            sw_show(r);
            r->eax = 1;
            SETW(r->edx, 0);
            return;
        }
        sw_wait = 1;
        sw_base = sw_tabs;
    }
    if (sw_tabs != sw_base) {
        sw_wait = 0;
        sw_show(r);
        r->eax = 1;
        SETW(r->edx, 0);
        return;
    }
    /* Alt let go: its release among the VM's message-mode keys (VKD) */
    for (;;) {
        struct svcregs k;
        memset(&k, 0, sizeof(k));
        k.ebx = VMH(v);
        vxd_service_call(0x000D, 0x0C, &k);     /* VKD_Get_Msg_Key */
        if (k.eflags & EFL_ZF) break;
        if ((k.eax & 0xFF) == 0xB8) up = 1;
        if ((k.eax & 0xFF) == 0x01) sw_esc = 1;
    }
    if (!up && !sw_esc) {
        vxdapi_again(r);
        return;
    }
    sw_wait = 0;
    sw_restore();
    {
        struct vm *to = sw_ebx == 0xFFFFFFFFu ? NULL : vm_of(sw_ebx);
        focus3(to ? to : &sysvm, r->edi, 1);
    }
    vxd_send(END_MESSAGE_MODE, v, 0, 0, 0);
    if (sh_of(v)) sh_of(v)->hk_pending = 0;     /* its keys again */
    r->eax = 0;
    r->ebx = sw_ebx;
    SETW(r->ecx, sw_ecx);
    SETW(r->edx, 1);
    sw_to_windows = sw_ebx == 0xFFFFFFFFu;
    sw_vm = 0;
    switch_key = 0;
    switch_wnd = 0;
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
            u8 *b = cl_ptr(r->es, r->edi, 48, 0);
            if (!b) { r->eflags |= EFL_CF; return; }
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
        if (dpmi_verbose && cl_ptr(r->es, r->edi, 0x50, 0)) {
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
            sh_mode(s);
        }
        return;
    case 0x07:                          /* the block's status word */
        if ((s = sh_of(vm_of(r->ebx))) != NULL && lin_user_ok(s->block, 2, 1))
            *(u16 *)s->block = (u16)((*(u16 *)s->block & ~0x000Fu) | (AX(r) & 0x000F));
        return;
    case 0x15: {                        /* WINOA386's window, for the VM just created */
        /* which VM: EBX the VM's handle (w16's WINOA386), else the one
         * whose function 3 came from the data segment in BX (Microsoft's
         * WINOA386 passes its DS there: its block for function 3 was in
         * it), else the oldest without a window. Before, the first without
         * one: three boxes started together (MEMLOAD C) had their windows
         * given to the wrong VMs, and a box whose program ended destroyed
         * another box's VM while it ran */
        struct shvm *t, *old = NULL;
        v = vm_of(r->ebx);
        for (s = shvms; s && (s->hwnd || !v || s->vm != v); s = s->next)
            ;
        if (!s)
            for (t = shvms; t; t = t->next)
                if (!t->hwnd && t->caller_ds == BX(r)) s = t;      /* the oldest such */
        if (!s) {
            for (t = shvms; t; t = t->next)
                if (!t->hwnd) old = t;
            s = old;
        }
        if (s) s->hwnd = SI(r);
        /* a windowed VM started as an icon (WinExec with SW_SHOWMINNOACTIVE)
         * gets no function 5: it runs now, without the focus (Windows 3.1,
         * VXT.386: Create_VM, VM_Resume, VM_Critical_Init, VM_Init with
         * nothing between; its Background then decides whether it gets
         * time). A full-screen one waits for the focus (its video memory is
         * the VGA's) */
        if (s && !s->vm->started && (s->flags & SHELL_WINDOWED)) resume_vm(s->vm);
        r->eax = 0;
        trace("SHELL: VM %d's window %x\n", s ? s->vm->id : 0, SI(r));
        return;
    }
    case 0x0C:                          /* an event record back */
        /* EAX 1 (after a switching key, Windows 3.1): the devices' focus
         * and the execution focus to the System VM, EAX 1 back. (After
         * Alt+Tab let go on a Windows window Microsoft's WINOA386 passes
         * EAX 0 here under w16's SHELL, 1 under 3.1's: UNVERIFIED why;
         * the System VM's either way, as on 3.1) */
        if (AX(r) == 1 || sw_to_windows) {
            sw_to_windows = 0;
            focus3(&sysvm, 0, 1);
            vm_set_focus(&sysvm);
            r->eax = 1;
        } else {
            /* EAX 0: back to the record's VM (+08h), which goes on
             * (Windows 3.1, VXSH: Alt+Tab let go on the VM's own title,
             * the devices' focus to it and Resume_VM) */
            struct shev *e = (struct shev *)r->edi;
            struct vm *b = evs && e >= evs && e < evs + NEVENTS ? vm_of(e->vm) : NULL;
            /* (not a VM whose program has ended: its record comes
             * back that way too before it is destroyed) */
            if (b && sh_of(b) && !sh_of(b)->ended_posted && b->id) set_focus(b, 0);
            r->eax = 0;
        }
        return;
    case 0x10:                          /* after event 41Eh: which key */
        if (switch_key == HK_ALTTAB && sw_vm) {
            alt_tab_title(r);
            return;
        }
        /* Windows 3.1 (VXSH): EBX FFFFFFFFh first: Alt+Tab EAX 1, EDX 0
         * (WINOA386 calls again with EBX the VM: the devices' focus to the
         * System VM, message mode ended (control 11h), EAX 0, EDX 1);
         * Alt+Esc EAX 0, EDX 0, the devices' focus to the System VM.
         * Ctrl+Esc: UNVERIFIED, as Alt+Esc */
        if (r->ebx == 0xFFFFFFFFu && !switch_wnd) switch_wnd = CX(r);   /* the first offered */
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
        switch_wnd = 0;
        return;
    case 0x14:                          /* (seen after a switch: nothing back) */
        return;
    case 0x16:
        /* after the fault event (41Bh) Windows 3.1 answered EAX 00010004h,
         * ECX 0083h (VXSH.386, VX12 G: WINOA386 calls it with the event's
         * record and then names the fault, "due to an invalid general
         * protection fault"); UNVERIFIED what the values mean */
        if (last_fault) {
            r->eax = 0x00010004u;
            SETW(r->ecx, 0x0083);
        }
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

/* A fault ended the VM (vm.c): Windows 3.1's sequence (vm_crashed), the
 * event 41Bh with wParam 4 */
void shell_vm_crashed(struct vm *v)
{
    struct shvm *s = sh_of(v);
    suspend_vm(v);
    vxd_send(VM_NOT_EXECUTEABLE, v, 1, 0, 0);
    if (vm_focus == v) {
        focus3(&sysvm, 0, 1);
        vm_set_focus(&sysvm);
    }
    if (s && !s->ended_posted) {
        s->ended_posted = 1;
        post(v, 0x41B, 4);
    }
}

/* Nuke_VM of a DOS VM (vm.c). Windows 3.1, a device calling Nuke_VM from
 * the System VM (VMTASK.386 under VXSH.386, tests/vmtaskrun.sh,
 * ARCHITECTURE.md 15.12): VM_Suspend, VM_Not_Executeable with EDX 2, then
 * SHELL's event 418h to the VM's WINOA386 (as for a program that ended),
 * which destroys the VM (function 4) and closes its window. The same
 * order as its Settings dialog's Terminate (WINOA386 calls function 4
 * with EAX 1 at once there) */
void shell_vm_nuked(struct vm *v)
{
    struct shvm *s = sh_of(v);
    if (v->ended) return;
    v->ended = 1;
    if (vm_focus == v) {
        focus3(&sysvm, 0, 1);
        vm_set_focus(&sysvm);
    }
    suspend_vm(v);
    vxd_send(VM_NOT_EXECUTEABLE, v, 2, 0, 0);
    if (s && !s->ended_posted) {
        s->ended_posted = 1;
        post(v, 0x418, 0);
    }
}

/* Close_VM of a DOS VM (vm.c): Close_VM_Notify to the devices, then its
 * programs end as at Ctrl+Alt+Del's Enter (link_close below; the DDK:
 * every device may clean up) */
void shell_vm_closing(struct vm *v)
{
    if (v->ended || v->closing) return;
    vxd_send(CLOSE_VM_NOTIFY, v, 0, 0, 0);
    vm_close(v);
}

/* REBOOT's link (w16vxd.h): the title, and closing the VM */
static int link_title(vu32 h, char *buf, int n)
{
    struct shvm *s = sh_of(vm_of(h));
    int i;
    if (!s || !s->title[0] || n < 1) return 0;
    for (i = 0; i < n - 1 && s->title[i]; i++) buf[i] = s->title[i];
    buf[i] = 0;
    return 1;
}

/* Windows 3.1 (VXSH.386, Ctrl+Alt+Del's Enter with the MS-DOS Prompt in
 * front): SHELL's event 415h, Close_VM_Notify (19h) to the devices, then
 * the VM's program ends (vm_close) and the VM with it: VM_Not_Executeable,
 * the focus to the System VM, event 418h (shell_vm_ended) */
static void link_close(vu32 h)
{
    struct vm *v = vm_of(h);
    if (!v || v == &sysvm || v->ended) return;
    post(v, 0x415, 0);
    vxd_send(CLOSE_VM_NOTIFY, v, 0, 0, 0);
    vm_close(v);
}

struct reboot_link *reboot_link_of(u32 ref)
{
    struct reboot_link *l = (struct reboot_link *)ref;
    if (!l || l->magic != REBOOT_LINK_MAGIC) return 0;
    l->title = link_title;
    l->close = link_close;
    return l;
}

/* the VM's program has ended (vm.c, in that VM): Windows 3.1's sequence,
 * then the event for WINOA386 */
/* VM_Init for a new VM, after the devices had it: VMPOLL's idle
 * detection from the PIF (Windows 3.1's SHELL, VXT.386: VMPoll_Get_Version,
 * then VMPoll_Enable_Disable with AL 1 for Detect Idle Time on, 0 off, EBX
 * the VM, while it handles VM_Init) */
void shell_vm_init(struct vm *v)
{
    struct shvm *sh = sh_of(v);
    struct svcregs s;
    if (!sh) return;
    memset(&s, 0, sizeof(s));
    if (!vxd_service_call(0x0018, 0x00, &s)) return;
    memset(&s, 0, sizeof(s));
    s.eax = 0x0300 | (sh->idle_detect ? 1 : 0);
    s.ebx = VMH(v);
    vxd_service_call(0x0018, 0x01, &s);
}

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
