/*
 * tk76.c - faults for w16's fault report (kernel/fault.c): a program that
 * faults with a stack the report must not trust. Visual C++ 1.52's WPS
 * and OLE2VIEW faulted on w16 and the report died after "w16: exc"; the
 * report now runs on a stack of its own and checks every read.
 * The first letter of C:\TK76.ARG picks the fault (WIN.INI's run= takes
 * no arguments: its blanks separate programs):
 *   S  SS a 32-byte data segment, SP 10h (a word pushed), BP 7777h
 *      (outside it), ES null, then DS loaded with FFFFh: a general
 *      protection fault (0Dh, error code FFFCh)
 *   P  SP 0 on the program's own stack, then a divide by zero (0)
 *   X  SS an expand-down segment (its valid offsets above its limit,
 *      0FFFh), SP FF00h (a word pushed), BP FF10h, DS and ES null, then
 *      a divide by zero
 * The boot-test case faultrep runs each from WIN.INI run= on w16's tree
 * and reads KERNEL's report from WIN's standard output. Nothing of 3.1 is
 * compared: Windows 3.1 shows its own box for a fault and goes on.
 */
#include <windows.h>

static WORD dpmi_alloc(void);
#pragma aux dpmi_alloc = "xor ax, ax" "mov cx, 1" "int 31h" value [ax] modify [cx];
static DWORD dpmi_base(WORD sel);
#pragma aux dpmi_base = "mov ax, 6" "int 31h" parm [bx] value [cx dx] modify [ax];
static void dpmi_set_base(WORD sel, WORD hi, WORD lo);
#pragma aux dpmi_set_base = "mov ax, 7" "int 31h" parm [bx] [cx] [dx] modify [ax];
static void dpmi_set_limit(WORD sel, WORD hi, WORD lo);
#pragma aux dpmi_set_limit = "mov ax, 8" "int 31h" parm [bx] [cx] [dx] modify [ax];
static void dpmi_set_rights(WORD sel, WORD rights);
#pragma aux dpmi_set_rights = "mov ax, 9" "int 31h" parm [bx] [cx] modify [ax];

/* SS = sel, SP = sp, BP = bp, DS = ES = 0, then the fault: DS loaded
   with a selector past the end of the LDT (a general protection fault,
   0Dh, error code FFFCh), or a divide by zero (0). (DOSBox's processor
   checks neither a null selector's use nor a segment's limit, and
   386 Enhanced mode takes HLT for an idle VM: these two fault on any.) */
static void fault_ss_gp(WORD sel, WORD sp, WORD bp);
#pragma aux fault_ss_gp = \
    "mov ss, ax" \
    "mov sp, dx" \
    "mov bp, bx" \
    "push ax" \
    "xor ax, ax" \
    "mov es, ax" \
    "mov ds, ax" \
    "mov ax, 0FFFFh" \
    "mov ds, ax" \
    "l1: jmp l1" \
    parm [ax] [dx] [bx] aborts;

static void fault_ss_div(WORD sel, WORD sp, WORD bp);
#pragma aux fault_ss_div = \
    "mov ss, ax" \
    "mov sp, dx" \
    "mov bp, bx" \
    "push ax" \
    "xor ax, ax" \
    "mov ds, ax" \
    "mov es, ax" \
    "xor dx, dx" \
    "xor cx, cx" \
    "div cx" \
    "l2: jmp l2" \
    parm [ax] [dx] [bx] aborts;

/* SP = 0 on the own stack, then a divide by zero */
static void fault_sp0(void);
#pragma aux fault_sp0 = \
    "mov bp, sp" \
    "xor sp, sp" \
    "xor ax, ax" \
    "xor dx, dx" \
    "xor cx, cx" \
    "div cx" \
    "l3: jmp l3" \
    aborts;

/* a 64K block's selector set up as a data segment of the given limit
   and rights (the block is its memory, so no write goes elsewhere) */
static WORD seg(WORD limit, WORD rights)
{
    HGLOBAL g = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 0x10000L);
    WORD s = dpmi_alloc();
    DWORD b = dpmi_base(SELECTOROF(GlobalLock(g)));
    dpmi_set_base(s, HIWORD(b), LOWORD(b));
    dpmi_set_rights(s, rights);
    dpmi_set_limit(s, 0, limit);
    return s;
}

int __cdecl tmain(void)
{
    char c = 0;
    HFILE f = _lopen("C:\\TK76.ARG", OF_READ);
    if (f != HFILE_ERROR) {
        _lread(f, &c, 1);
        _lclose(f);
    }
    switch (c) {
    case 'S': case 's':
        fault_ss_gp(seg(0x1F, 0xF2), 0x10, 0x7777);
        break;
    case 'P': case 'p':
        fault_sp0();
        break;
    case 'X': case 'x':
        fault_ss_div(seg(0x0FFF, 0xF6), 0xFF00, 0xFF10);
        break;
    }
    return 0;
}
