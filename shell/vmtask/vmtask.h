/* vmtask.h - VM Task Manager (VMTASK.EXE): resource IDs, VMTASK.386's
   interface (win386/vxd/vmtask.c) and vmtaskhk.asm's (ARCHITECTURE.md
   15.12) */
#define IDI_VMTASK      1
#define IDM_MENU        1
#define IDS_TITLE       1

#define IDM_INT500      100
#define IDM_INT1000     101
#define IDM_INT2000     102
#define IDM_INT5000     103
#define IDM_ONTOP       110
#define IDM_TASKS       111
#define IDM_GRAPH       112
#define IDM_REFRESH     113
#define IDM_IDS         114
#define IDM_SWITCH      140
#define IDM_ENDTASK     141
#define IDM_ENDVM       142
#define IDM_END         143
#define IDM_DETAILS     144
#define IDM_COPY        145
#define IDM_CONTEXT     146
#define IDM_VXDS        147
#define IDM_DRIVERS     148
#define IDM_EXIT        120
#define IDM_ABOUT       130

#define IDC_LIST        200
#define IDD_TERMINATE   300
#define IDC_STOPICON    301
#define IDD_VXDS        310
#define IDD_DRIVERS     311
#define IDC_ITEMS       312
#define IDC_REFRESH     313

#ifndef RC_INVOKED
/* VMTASK.386 (device 5716h): function 1's snapshot */
#define VMT_DEVICE      0x5716
#pragma pack(1)
struct vmt_head {
    WORD head_size, rec_size, vm_count, rec_count;
    DWORD sys_time, idle_ms, idle_calls, sys_vm, focus_vm, granularity;
};
struct vmt_vm {
    DWORD handle, id, status, run_ms, exec_ms, ts_flags, fg_prio, bg_prio;
    DWORD cpu_pct, shell_flags, created, runs;
    DWORD pages, pages_out, ems_max, xms_max;   /* (records of 64 bytes) */
};
/* function 4: the loaded devices */
struct vmt_dev {
    char name[8];
    WORD id;
    BYTE major, minor;
    DWORD init_order, services, flags;
};
#pragma pack()
/* CB_VM_Status bits (as Windows 3.1's control blocks showed them,
   ARCHITECTURE.md 12.12) */
#define VMS_EXCLUSIVE   0x0001
#define VMS_BACKGROUND  0x0002
#define VMS_SUSPENDED   0x0008
#define VMS_NOTEXEC     0x0010
#define VMS_IDLE        0x8000
#define SGVMI_WINDOWED  0x0004          /* SHELL_GetVMInfo (observed) */

/* vmtaskhk.asm */
#define VMT_NSLOTS 64
#pragma pack(1)
struct vmt_slot { WORD task; DWORD ms; };
#pragma pack()
extern DWORD __cdecl vmt_entry;
extern struct vmt_slot __cdecl vmt_slots[VMT_NSLOTS];
extern WORD __cdecl vmt_cur;
extern DWORD __cdecl vmt_tin;
extern DWORD __cdecl vmt_hidle, vmt_hcalls;
extern void __cdecl __far vmt_init(void);
extern void __cdecl __far vmt_hook(int on);
extern int __cdecl __far vmt_api(WORD FAR *regs);
extern DWORD __cdecl __far vmt_find(WORD id);
extern BOOL FAR PASCAL NotifyProc(WORD id, DWORD data);
#endif
