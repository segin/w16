/*
 * kernel.h - internal declarations for the w16 KERNEL module.
 *
 * Build model: large model, SS != DS (-zu), 286 instructions, no C runtime.
 * Exported API functions use KAPI: far, Pascal calling convention, and
 * __loadds so DS is KERNEL's DGROUP regardless of the caller's DS.
 *
 * Details recalled from published descriptions but not yet checked against
 * real Windows 3.1 are marked "UNVERIFIED:" (grep for it).
 */
#ifndef W16_KERNEL_H
#define W16_KERNEL_H

#include "w16/types.h"
#include "w16/ne.h"
#include "regs.h"
#include "bootinfo.h"

#define FAR     __far
#define KAPI    __far __pascal __loadds
#define NULL    ((void *)0)

#define MK_FP(s, o)  ((void __far *)(((u32)(u16)(s) << 16) | (u16)(o)))
#define FP_SEG(p)    ((u16)((u32)(void __far *)(p) >> 16))
#define FP_OFF(p)    ((u16)(u32)(void __far *)(p))

/* ---- asm helpers (kentry.asm) ---- */
extern int  __cdecl dos21(regs16 FAR *r);       /* returns carry */
extern int  __cdecl dpmi31(regs16 FAR *r);      /* returns carry */
extern u16  __cdecl task_run(u16 cs, u16 ip, u16 ds, u16 ss, u16 sp,
                             u16 bx, u16 cx, u16 di, u16 si, u16 es);
extern void __cdecl seg_move(u16 sel, u16 tmp, u16 nwords, u32 newbase);
extern void __far int21_handler(void);
extern void __far int3d_handler(void);
#pragma aux int3d_handler "*"
extern void __far unimpl_entry(void);
#pragma aux int21_handler "*"           /* asm labels: no name decoration */
#pragma aux unimpl_entry "*"
extern u32  __cdecl old21;
extern u16  __cdecl task_active;

/* ---- util.c ---- */
void kmemset(void FAR *d, u8 v, u16 n);
void kmemcpy(void FAR *d, const void FAR *s, u16 n);
u16  kstrlen(const char FAR *s);
int  kstricmp(const char FAR *a, const char FAR *b);
int  kstrnicmp_p(const char FAR *a, const u8 FAR *pstr);  /* vs length-prefixed */
void kputs(const char FAR *s);
void kputhex(u16 v);
void kputdec(u16 v);
void kexit_to_dos(u8 code);

/* ---- dpmi.c ---- */
extern u16 cpl;                 /* privilege level we run at */
extern u16 sel_inc;             /* selector increment (__AHINCR) */
extern u16 sel_shift;           /* log2(sel_inc) (__AHSHIFT) */
u16  dpmi_alloc_desc(u16 n);
void dpmi_free_desc(u16 sel);
int  dpmi_set_desc(u16 sel, u32 base, u32 limit, int code);
u32  dpmi_get_base(u16 sel);
u16  dpmi_seg_to_desc(u16 para);
int  dpmi_alloc_mem(u32 size, u32 *lin, u32 *handle);
int  dpmi_free_mem(u32 handle);
int  dpmi_resize_mem(u32 handle, u32 size, u32 *lin, u32 *newhandle);
u16  dpmi_cpu_type(void);

/* ---- global.c ---- */
#define GMEM_FIXED          0x0000
#define GMEM_MOVEABLE       0x0002
#define GMEM_NOCOMPACT      0x0010
#define GMEM_NODISCARD      0x0020
#define GMEM_ZEROINIT       0x0040
#define GMEM_MODIFY         0x0080
#define GMEM_DISCARDABLE    0x0100
#define GMEM_NOT_BANKED     0x1000
#define GMEM_SHARE          0x2000
#define GMEM_NOTIFY         0x4000
#define GMEM_LOCKCOUNT      0x00FF
#define GMEM_DISCARDED      0x4000

void global_init(void);
u16  galloc(u16 flags, u32 size, u16 owner, u16 *sel_out); /* returns handle */
int  gset_code(u16 sel);                    /* make a block executable */
u32  gsize_of_sel(u16 sel);
u16  KAPI GlobalReAlloc(u16 h, u32 size, u16 flags);
u16  KAPI GlobalFree(u16 h);
u32  KAPI GlobalSize(u16 h);
u32  KAPI GlobalHandle(u16 sel);

/* ---- local.c ---- */
int  local_init_task(u16 ds, u16 heap);

/* ---- module.c ---- */
#define DOS_PATHMAX     128     /* DOS: longest full path (TRUENAME buffer) */
#define MODNAME_MAX     8       /* NE: module names are at most 8 chars */

typedef struct modseg {
    u16 sel;                    /* selector of the segment */
    u16 hnd;                    /* its global handle */
} modseg;

/* One per loaded module, in its own global block; modules form a list
   (module_first). seg[] has seg_count entries and is followed by
   dep_count words: the hModules of the modules this one imports from
   (0 for imports bound to "unimplemented" thunks). */
typedef struct module {
    struct module FAR *next;
    u16  self;                  /* selector of this block */
    char name[MODNAME_MAX + 1]; /* module name, upper case */
    u16  ne_sel;                /* resident NE tables; also the hModule */
    u16  ne_len;
    u16  nonres_sel;            /* non-resident name table, 0 if none */
    u16  flags;                 /* NE header flags (NEF_*) */
    u16  usage;
    u16  seg_count;
    u16  dep_count;
    char path[DOS_PATHMAX];
    modseg seg[1];              /* seg_count entries, then the deps */
} module;

#define NE_HDR(m)   ((ne_header FAR *)MK_FP((m)->ne_sel, 0))
#define MOD_DEPS(m) ((u16 FAR *)&(m)->seg[(m)->seg_count])

extern module FAR *module_first;        /* KERNEL */
void     module_register_kernel(const bootinfo FAR *bi);
module FAR *module_load_program(const char FAR *path, u16 *err);
module FAR *module_find(const char FAR *name);
module FAR *module_of(u16 h);
u16      module_instance(module FAR *m);
int      module_entry(module FAR *m, u16 ordinal, u16 *sel, u16 *off, int *is_const);
module FAR *mod_new(u16 seg_count, u16 dep_count);
void     mod_link(module FAR *m);
void     mod_unlink(module FAR *m);
void     mod_set_name(module FAR *m);
void     mod_set_path(module FAR *m, const char FAR *path);
int      mod_find_name(module FAR *m, const u8 FAR *pname, u16 *ordinal);
int      mod_base_name(const char FAR *name, char *base);

/* ---- loader.c ---- */
void     loader_init(const char FAR *kernel_path);

/* far-call cs:ip as KERNEL calls a DLL's entry point (AX = 1, CX, DX =
   DS, SI = 0, DI, ES = 0, DS), with one Pascal word argument if nargs;
   returns AX (kentry.asm) */
extern u16 __cdecl mod_call(u16 cs, u16 ip, u16 ds, u16 cx, u16 di,
                            u16 nargs, u16 arg);

/* ---- thunk.c ---- */
void thunk_init(void);
int  thunk_make(const char FAR *mod, u16 ordinal, const u8 FAR *pname,
                u16 *sel, u16 *off);
void __cdecl unimpl_report(u16 index);

/* ---- task.c ---- */
u16  task_start(module FAR *m, const char FAR *cmdtail);
module FAR *task_module(void);          /* the running task's, or NULL */
void __cdecl inittask_c(regs16 FAR *r);
u16  get_winflags(void);
#define WF_PMODE        0x0001
#define WF_CPU286       0x0002
#define WF_CPU386       0x0004
#define WF_CPU486       0x0008
#define WF_STANDARD     0x0010
#define WF_ENHANCED     0x0020
#define WF_80x87        0x0400
extern u16 psp_sel;

#endif
