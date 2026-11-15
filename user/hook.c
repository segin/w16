/*
 * hook.c - window hooks (SetWindowsHook, SetWindowsHookEx, CallNextHookEx,
 * DefHookProc, UnhookWindowsHook(Ex), CallMsgFilter).
 *
 * Each hook type (WH_MSGFILTER -1 .. WH_SHELL 10) has a chain, newest
 * first; a hook is task-local when installed with a task, else system
 * wide. w16 calls WH_MSGFILTER/WH_SYSMSGFILTER from CallMsgFilter, and
 * WH_SHELL with HSHELL_WINDOWCREATED/DESTROYED/ACTIVATESHELLWINDOW (3.1
 * SDK); the other types are kept but not yet called (UNVERIFIED which
 * places Windows 3.1 calls them from).
 *
 * A hook is a block of the object heap (uheap.c; 3.1 keeps its 10h-byte
 * hook blocks in DGROUP: US15), reached through HK(); no pointer into the
 * heap is kept across a call to a hook procedure.
 */
#include "user.h"

#define NHOOKTYPES 12           /* WH_MSGFILTER (-1) .. WH_SHELL (10) */

typedef struct HOOK {
    WORD   next;
    int    type;
    HOOKPROC proc;
    HTASK  task;                /* 0: every task */
    HINSTANCE hinst;
    WORD   old;                 /* installed with the 3.0 SetWindowsHook */
} HOOK;
typedef HOOK __far *PHOOK;
#define HK(h)   ((PHOOK)UH_PTR(&uhObj, (h)))

static WORD chains[NHOOKTYPES];

static WORD *chain(int type)
{
    return (type >= -1 && type <= 10) ? &chains[type + 1] : 0;
}

static WORD add_hook(int type, HOOKPROC proc, HINSTANCE hinst, HTASK task, BOOL old)
{
    WORD *c = chain(type), hh;
    PHOOK h;
    if (!c || !proc)
        return 0;
    hh = uh_alloc(&uhObj, LMEM_FIXED | LMEM_ZEROINIT, sizeof(HOOK));
    if (!hh)
        return 0;
    h = HK(hh);
    h->type = type;
    h->proc = proc;
    h->hinst = hinst;
    h->task = task;
    h->old = old;
    h->next = *c;
    *c = hh;
    return hh;
}

HHOOK WINAPI SetWindowsHookEx(int type, HOOKPROC proc, HINSTANCE hinst, HTASK task)
{
    WORD h = add_hook(type, proc, hinst, task, FALSE);
    return h ? (HHOOK)MAKELONG(h, 0x4B48) : 0;          /* "HK" */
}

/* the 3.0 interface: returns the previous hook procedure (3.1 SDK), which
   the caller passes to DefHookProc */
HOOKPROC WINAPI SetWindowsHook(int type, HOOKPROC proc)
{
    WORD *c = chain(type);
    HOOKPROC prev = c && *c ? HK(*c)->proc : 0;
    if (!add_hook(type, proc, 0, 0, TRUE))
        return 0;
    return prev;
}

/* a hook block of ours: in the heap, its type's chain has it */
static BOOL unhook(WORD h)
{
    WORD *c, __far *l;
    if (!h || (h & 1) || uh_size(&uhObj, h) < sizeof(HOOK))
        return FALSE;
    c = chain(HK(h)->type);
    if (!c)
        return FALSE;
    for (l = c; *l; l = &HK(*l)->next)
        if (*l == h) {
            *l = HK(h)->next;
            uh_free(&uhObj, h);
            return TRUE;
        }
    return FALSE;
}

BOOL WINAPI UnhookWindowsHookEx(HHOOK hh)
{
    return HIWORD((DWORD)hh) == 0x4B48 && unhook(LOWORD((DWORD)hh));
}

BOOL WINAPI UnhookWindowsHook(int type, HOOKPROC proc)
{
    WORD *c = chain(type), h;
    if (!c)
        return FALSE;
    for (h = *c; h; h = HK(h)->next)
        if (HK(h)->proc == proc)
            return unhook(h);
    return FALSE;
}

static LRESULT call_from(WORD h, int code, WPARAM wp, LPARAM lp)
{
    HTASK me = GetCurrentTask();
    for (; h; h = HK(h)->next)
        if (!HK(h)->task || HK(h)->task == me) {
            HOOKPROC f = HK(h)->proc;
            return ((LRESULT (__far __pascal *)(int, WPARAM, LPARAM))f)(code, wp, lp);
        }
    return 0;
}

LRESULT WINAPI CallNextHookEx(HHOOK hh, int code, WPARAM wp, LPARAM lp)
{
    WORD h = LOWORD((DWORD)hh);
    if (HIWORD((DWORD)hh) != 0x4B48 || !h || (h & 1) || uh_size(&uhObj, h) < sizeof(HOOK))
        return 0;
    return call_from(HK(h)->next, code, wp, lp);
}

/* the 3.0 interface: next is the HOOKPROC far pointer stored by the
   caller (lpfnNext: the address of that variable) */
LRESULT WINAPI DefHookProc(int code, WPARAM wp, LPARAM lp, HOOKPROC __far *next)
{
    int t;
    if (!next || !*next)
        return 0;
    for (t = 0; t < NHOOKTYPES; t++) {
        WORD h;
        for (h = chains[t]; h; h = HK(h)->next)
            if (HK(h)->proc == *next) {
                HOOKPROC f = HK(h)->proc;
                return ((LRESULT (__far __pascal *)(int, WPARAM, LPARAM))f)(code, wp, lp);
            }
    }
    return 0;
}

LRESULT call_hook(int type, int code, WPARAM wp, LPARAM lp)
{
    WORD *c = chain(type);
    return c && *c ? call_from(*c, code, wp, lp) : 0;
}

BOOL WINAPI CallMsgFilter(MSG __far *m, int code)
{
    if (call_hook(WH_SYSMSGFILTER, code, 0, (LPARAM)m))
        return TRUE;
    return call_hook(WH_MSGFILTER, code, 0, (LPARAM)m) != 0;
}

/* WH_SHELL notifications (3.1 SDK: HSHELL_*) */
void shell_notify(int code, HWND h)
{
    call_hook(WH_SHELL, code, (WPARAM)h, 0);
}

/* a hook of this type is set (for any task) */
BOOL hook_set(int type)
{
    WORD *c = chain(type);
    return c && *c;
}
