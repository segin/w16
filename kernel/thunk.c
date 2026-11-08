/*
 * thunk.c - stand-ins for imports that cannot be resolved.
 *
 * Each thunk is 8 bytes of code:   B8 lo hi        mov ax, <index>
 *                                  EA ofs seg      jmp far unimpl_entry
 * unimpl_entry (kentry.asm) reports the call and ends the task with exit
 * code 0FFh. Loading never fails just because an API is missing; the gap
 * shows up only if the program actually calls it.
 */
#include "kernel.h"

#define MAX_THUNKS 256
#define THUNK_SIZE 8

typedef struct thunkinfo {
    char mod[9];
    u16  ordinal;               /* 0 when imported by name */
    char name[33];
} thunkinfo;

static thunkinfo info[MAX_THUNKS];
static u16 count;
static u16 data_sel, code_sel;

void thunk_init(void)
{
    u32 size = (u32)MAX_THUNKS * THUNK_SIZE;
    count = 0;
    if (!galloc(GMEM_FIXED, size, 0, &data_sel))
        return;
    code_sel = dpmi_alloc_desc(1);
    if (code_sel && !dpmi_set_desc(code_sel, dpmi_get_base(data_sel), size - 1, 1))
        code_sel = 0;
}

int thunk_make(const char FAR *mod, u16 ordinal, const u8 FAR *pname,
               u16 *sel, u16 *off)
{
    u16 i, n;
    u8 FAR *code;
    char name[33];
    void (__far *target)(void) = unimpl_entry;

    if (!code_sel)
        return 0;
    name[0] = 0;
    if (pname) {
        n = pname[0] < 32 ? pname[0] : 32;
        for (i = 0; i < n; i++)
            name[i] = (char)pname[1 + i];
        name[n] = 0;
    }
    for (i = 0; i < count; i++)
        if (info[i].ordinal == ordinal && kstricmp(info[i].mod, mod) == 0 &&
            kstricmp(info[i].name, name) == 0)
            goto found;
    if (count == MAX_THUNKS)
        return 0;
    i = count++;
    kmemcpy(info[i].mod, mod, 9);
    info[i].mod[8] = 0;
    info[i].ordinal = ordinal;
    kmemcpy(info[i].name, name, sizeof name);
    code = MK_FP(data_sel, i * THUNK_SIZE);
    code[0] = 0xB8;
    code[1] = (u8)i;
    code[2] = (u8)(i >> 8);
    code[3] = 0xEA;
    code[4] = (u8)FP_OFF(target);
    code[5] = (u8)(FP_OFF(target) >> 8);
    code[6] = (u8)FP_SEG(target);
    code[7] = (u8)(FP_SEG(target) >> 8);
found:
    *sel = code_sel;
    *off = i * THUNK_SIZE;
    return 1;
}

void __cdecl unimpl_report(u16 index)
{
    kputs("w16: call to unimplemented ");
    if (index < count) {
        kputs(info[index].mod);
        if (info[index].name[0]) {
            kputs(".");
            kputs(info[index].name);
        } else {
            kputs(".");
            kputdec(info[index].ordinal);
        }
    }
    kputs("\r\n");
}
