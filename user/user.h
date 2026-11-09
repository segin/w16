/*
 * user.h - w16's USER.EXE, internal definitions (ARCHITECTURE.md 10).
 *
 * USER is a large-model DLL (SS != DS: exported functions run on their
 * callers' stacks). Its own data segment (DGROUP) holds the local heap
 * where windows (WND), classes, menus and properties live; an HWND is the
 * near offset of its WND there, as on Windows 3.1 (Pietrek, Windows
 * Internals, ch. 4; Schulman, Undocumented Windows). Exported functions are
 * written U_Name (UAPI: far pascal, DS = DGROUP); tools/mkuserexp.py
 * exports them under their Windows names and stubs every other ordinal.
 */
#ifndef W16_USER_H
#define W16_USER_H

#define OEMRESOURCE
#include <windows.h>
#include <dos.h>

#define UAPI    __far __pascal __loadds
#define UCB     __far __pascal          /* callbacks USER calls */
#define NEAR_P  __near

typedef WORD HQUEUE;

/* ---- imports that windows.h does not declare (user/user.lnk binds them
   by ordinal; sources: the NE import tables of Windows 3.1's USER
   (docs/user/imports.txt), names from docs/exports) ---- */
/* KERNEL */
extern void   __far __pascal K_FATALEXIT(int);                       /* 1 */
extern WORD   __far __pascal K_WAITEVENT(HTASK);                     /* 30 */
extern BOOL   __far __pascal K_POSTEVENT(HTASK);                     /* 31 */
extern HQUEUE __far __pascal K_SETTASKQUEUE(HTASK, HQUEUE);          /* 34 */
extern HQUEUE __far __pascal K_GETTASKQUEUE(HTASK);                  /* 35 */
extern FARPROC __far __pascal K_SETTASKSIGNALPROC(HTASK, FARPROC);   /* 38 */
extern HMODULE __far __pascal K_GETEXEPTR(HANDLE);                   /* 133 */
extern WORD   __far __pascal K_DIRECTEDYIELD(HTASK);                 /* 150 */
extern WORD   __far __pascal K_GETTASKDS(void);                      /* 155 */
extern WORD   __far __pascal K_GETEXPWINVER(HMODULE);                /* 167 */
extern void   __far __pascal K_DIAGOUTPUT(LPCSTR);                   /* 340 */
extern WORD   __far __pascal K_OLDYIELD(void);                       /* 117 */
/* GDI */
extern BOOL  __far __pascal G_SETDCORG(HDC, int, int);               /* 117 */
extern int   __far __pascal G_SELECTVISRGN(HDC, HRGN);               /* 105 */
extern HDC   __far __pascal G_GETDCSTATE(HDC);                       /* 179 */
extern void  __far __pascal G_SETDCSTATE(HDC, HDC);                  /* 180 */
extern void  __far __pascal G_MAKEOBJECTPRIVATE(HANDLE, BOOL);       /* 463 */
extern BOOL  __far __pascal G_ISGDIOBJECT(HANDLE);                   /* 462 */
extern void  __far __pascal G_FINALGDIINIT(HBRUSH);                  /* 405 */
extern HRGN  __far __pascal G_INQUIREVISRGN(HDC);                    /* 131 */
extern void  __far __pascal G_GDITASKTERMINATION(HTASK);             /* 460 */
/* SYSTEM */
extern WORD  __far __pascal S_CREATESYSTEMTIMER(WORD, FARPROC);      /* 2 */
extern WORD  __far __pascal S_KILLSYSTEMTIMER(WORD);                 /* 3 */
extern void  __far __pascal S_ENABLESYSTEMTIMERS(void);              /* 4 */
extern DWORD __far __pascal S_GETSYSTEMMSECCOUNT(void);              /* 6 */
/* KEYBOARD, MOUSE, DISPLAY (the driver interfaces of the 3.1 DDK) */
extern WORD  __far __pascal KB_INQUIRE(void __far *);                /* 1 */
extern void  __far __pascal KB_ENABLE(FARPROC, BYTE __far *);        /* 2 */
extern void  __far __pascal KB_DISABLE(void);                        /* 3 */
extern int   __far __pascal KB_TOASCII(WORD, WORD, BYTE __far *, DWORD __far *, WORD); /* 4 */
extern int   __far __pascal KB_SETSPEED(int);                        /* 7 */
extern WORD  __far __pascal MS_INQUIRE(void __far *);                /* 1 */
extern void  __far __pascal MS_ENABLE(FARPROC);                      /* 2 */
extern void  __far __pascal MS_DISABLE(void);                        /* 3 */
extern WORD  __far __pascal D_INQUIRE(void __far *);                 /* 101 */
extern void  __far __pascal D_SETCURSOR(void __far *);               /* 102 */
extern void  __far __pascal D_MOVECURSOR(int, int);                  /* 103 */
extern void  __far __pascal D_CHECKCURSOR(void);                     /* 104 */

/* ---- uentry.asm ---- */
extern LONG __far __cdecl call_wndproc(WNDPROC f, WORD ax, HWND, UINT, WPARAM, LPARAM);
extern void __far __cdecl keybd_event_entry(void);   /* KEYBOARD's event procedure */
extern void __far __cdecl mouse_event_entry(void);   /* MOUSE's event procedure */
WORD get_ds(void);
#pragma aux get_ds = "mov ax, ds" value [ax];
WORD get_ss(void);
#pragma aux get_ss = "mov ax, ss" value [ax];
WORD irq_off(void);
#pragma aux irq_off = "pushf" "pop ax" "cli" value [ax];
void irq_restore(WORD);
#pragma aux irq_restore = "push ax" "popf" parm [ax];

/* ---- windows (win.c) ---- */
typedef struct WND {            /* layout: Pietrek, Windows Internals, table 4-2 */
    HWND   hwndNext;            /* 00 */
    HWND   hwndChild;           /* 02 */
    HWND   hwndParent;          /* 04 */
    HWND   hwndOwner;           /* 06 */
    RECT   rcWindow;            /* 08 screen coordinates */
    RECT   rcClient;            /* 10 screen coordinates */
    HQUEUE hq;                  /* 18 */
    HRGN   hrgnUpdate;          /* 1A window coordinates; 1: whole window */
    WORD   cls;                 /* 1C near pointer to the CLS */
    HINSTANCE hInstance;        /* 1E */
    WNDPROC lpfnWndProc;        /* 20 */
    DWORD  flags;               /* 24 WF_* */
    DWORD  style;               /* 28 */
    DWORD  exstyle;             /* 2C */
    WORD   hMenu;               /* 30 menu, or the child id */
    WORD   text;                /* 32 local handle of the window text (USER's heap) */
    WORD   scroll;              /* 34 scroll bar data (local handle) */
    WORD   props;               /* 36 property list (local handle) */
    HWND   hwndLastActive;      /* 38 */
    WORD   hSysMenu;            /* 3A */
    /* window extra bytes follow (cbWndExtra) */
} WND;
typedef WND __near *PWND;
#define WND_EXTRA(w)   ((BYTE __near *)((w) + 1))

/* WND.flags (w16's own) */
#define WF_UPDATENC     0x0001  /* the frame needs WM_NCPAINT */
#define WF_ERASEBKGND   0x0002  /* the update region needs WM_ERASEBKGND */
#define WF_INPAINT      0x0004  /* counted in its queue's paint count */
#define WF_DESTROYING   0x0008
#define WF_ACTIVEFRAME  0x0010  /* drawn active (WM_NCACTIVATE) */
#define WF_CREATED      0x0020
#define WF_MAXIMIZED    0x0040
#define WF_INTERNALPAINT 0x0080
#define WF_SENTCREATE   0x0100
#define WF_HASPOS       0x0200  /* SetWindowPlacement: restored rect known */
#define WF_DLG          0x0400  /* a dialog box (DLGWINDOWEXTRA) */

#define PW(h)   ((PWND)(WORD)(h))
PWND valid_wnd(HWND h);         /* NULL if h is not a live window */
extern HWND hwndDesktop;

/* ---- classes (class.c) ---- */
typedef struct CLS {
    WORD   next;                /* next class (near) */
    ATOM   atom;                /* name, an atom in USER's local atom table */
    WORD   style;
    WNDPROC lpfnWndProc;
    int    cbClsExtra, cbWndExtra;
    HINSTANCE hInstance;        /* 0: system class */
    HICON  hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;        /* as given (far pointer or resource id) */
    WORD   menuname;            /* local copy of a string menu name, or 0 */
    WORD   cwnd;                /* windows of the class */
    HMODULE hModule;
    WORD   name;                /* local copy of a string name (0: integer atom) */
    /* class extra bytes follow */
} CLS;
typedef CLS __near *PCLS;
PCLS find_class(LPCSTR name, HINSTANCE hinst);
void register_system_classes(void);

/* ---- queues (msg.c) ---- */
typedef struct QMSG {
    MSG    msg;
    DWORD  extra;
} QMSG;

typedef struct SMSG {           /* an inter-task SendMessage in flight */
    WORD   next;                /* near, in USER's heap */
    HQUEUE from;
    HWND   hwnd;
    UINT   msg;
    WPARAM wParam;
    LPARAM lParam;
    LONG   result;
    WORD   done;
} SMSG;
typedef SMSG __near *PSMSG;

typedef struct QUEUE {
    HQUEUE next;                /* 00 */
    HTASK  task;                /* 02 */
    WORD   wakebits;            /* QS_* available */
    WORD   changebits;
    WORD   wakemask;            /* what the task waits for (0: not waiting) */
    WORD   npaint;              /* windows with something to paint */
    WORD   quit;                /* PostQuitMessage called */
    int    quitcode;
    WORD   size, head, count;   /* the posted-message ring */
    WORD   sent;                /* SMSG list sent to this queue (near) */
    WORD   insend;              /* SMSG being handled, 0 if none */
    WORD   ntimer;              /* timers due */
    DWORD  msgextra;
    DWORD  msgpos, msgtime;     /* GetMessagePos/Time of the last message */
    HINSTANCE hinst;            /* InitApp's */
    WORD   expwinver;
    HWND   hwndActive, hwndFocus;
    QMSG   msgs[1];             /* size entries */
} QUEUE;
#define QS_SMRESULT 0x8000      /* w16: a reply for this queue has come */

QUEUE __far *q_of(HQUEUE hq);
HQUEUE cur_hq(void);
HQUEUE create_queue(HTASK task, WORD size);
void destroy_queue(HQUEUE hq);
void set_wakebit(HQUEUE hq, WORD bits);
void inc_paint(HQUEUE hq);
void dec_paint(HQUEUE hq);
LONG send_message(HWND h, UINT msg, WPARAM wp, LPARAM lp);
LONG call_wnd(PWND w, HWND h, UINT msg, WPARAM wp, LPARAM lp);
BOOL post_message(HQUEUE hq, HWND h, UINT msg, WPARAM wp, LPARAM lp);
extern HQUEUE hqFirst;
extern HQUEUE hqBoot;           /* the queue of the boot task */

/* ---- timers (timer.c) ---- */
void timer_init(void);
void kill_queue_timers(HQUEUE hq);
void kill_window_timers(HWND h);
BOOL timer_message(QUEUE __far *q, HQUEUE hq, HWND hfilter, UINT lo, UINT hi, BOOL remove, MSG __far *m);
extern volatile DWORD __far tickcount;   /* interrupt-time data: fixed segment */

/* ---- input (input.c) ---- */
void input_init(void);
void input_enable(void);
BOOL input_message(HQUEUE hq, HWND hfilter, UINT lo, UINT hi, BOOL remove, MSG __far *m);
extern BYTE __far keystate[256];
extern POINT __far ptCursor;
extern HWND hwndCapture, hwndFocus, hwndActive;
extern HQUEUE __far hqInput;          /* the queue input goes to (the active window's) */
extern WORD __far input_pending;
void set_cursor_shape(HCURSOR h);
void set_focus(HWND h);
void activate_window(HWND h, BOOL mouse);

/* ---- painting, DCs (paint.c, dc.c) ---- */
void dc_init(void);
HDC  get_dc(HWND h, BOOL window, HRGN clip, WORD flags);
int  release_dc(HWND h, HDC hdc);
HRGN visible_rgn(PWND w, BOOL client, BOOL clipchildren);
void invalidate(HWND h, HRGN rgn, const RECT *rc, BOOL erase, BOOL children, BOOL nc);
void validate_all(PWND w);
BOOL paint_message(HQUEUE hq, HWND hfilter, MSG __far *m);
void update_window(HWND h, BOOL children);
void redraw_screen(void);
extern HDC hdcScreen, hdcMem, hdcState;
extern HRGN hrgnTmp1, hrgnTmp2;

/* ---- window management (win.c, pos.c) ---- */
HWND create_window(DWORD exstyle, LPCSTR cls, LPCSTR title, DWORD style,
                   int x, int y, int cx, int cy, HWND parent, HMENU menu,
                   HINSTANCE hinst, void __far *param);
BOOL destroy_window(HWND h);
BOOL set_window_pos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags);
BOOL is_visible(PWND w);                /* the window and its ancestors */
void link_window(PWND w, HWND after);
void unlink_window(PWND w);
HWND top_level(HWND h);
void calc_client(PWND w);

/* ---- default window procedure, frames (defwnd.c, nc.c) ---- */
LONG def_window_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
void nc_paint(HWND h, HRGN rgn, BOOL active);
LONG nc_hittest(PWND w, POINT pt);
void nc_calcsize(PWND w, RECT *rc);

/* ---- menus (menu.c) ---- */
int  menu_bar_height(PWND w, int width);
void menu_draw_bar(HDC hdc, PWND w, int x, int y, int width);

/* ---- system values (init.c, sysmet.c) ---- */
extern int  sysmet[SM_CMETRICS + 8];
extern COLORREF syscolor[COLOR_BTNHIGHLIGHT + 1];
extern HBRUSH sysbrush[COLOR_BTNHIGHLIGHT + 1];
extern HBRUSH hbrGray;          /* the 50% gray pattern brush */
extern HINSTANCE hInstUser;
extern HMODULE hModUser;
extern HFONT hfontSys;
extern int cxSysChar, cySysChar;        /* system font average width, height */
extern WORD booted;                     /* FinalUserInit has run */

/* ---- strings, misc (str.c, misc.c) ---- */
WORD lstr_len(LPCSTR s);
void lstr_cpyn(LPSTR d, LPCSTR s, WORD n);
int  lstr_cmpi(LPCSTR a, LPCSTR b);
WORD local_strdup(LPCSTR s);    /* a local (USER heap) copy, 0 for NULL */
void diag(LPCSTR s);            /* DiagOutput, for BOOTLOG.TXT */
void ulog(LPCSTR s);            /* w16's own log (C:\USER.LOG, [w16] userlog=1) */
void ulogx(LPCSTR s, WORD v);
#define ISATOM(p)  (HIWORD((DWORD)(p)) == 0)
void fmemcpy(void __far *d, const void __far *s, WORD n);
void fmemset(void __far *d, BYTE v, WORD n);
#define iabs(x)    ((x) < 0 ? -(x) : (x))
#define WM_SYSTIMER 0x0118      /* SetSystemTimer's message (Wine, X11-era) */

/* ---- resources (icon.c) ---- */
HICON load_icon_cursor(HINSTANCE hinst, LPCSTR name, BOOL cursor);
void draw_icon(HDC hdc, int x, int y, HICON h);

#endif
