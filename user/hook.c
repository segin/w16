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
typedef HOOK __near *PHOOK;

static WORD chains[NHOOKTYPES];

static WORD *chain(int type)
{
    return (type >= -1 && type <= 10) ? &chains[type + 1] : 0;
}

static PHOOK add_hook(int type, HOOKPROC proc, HINSTANCE hinst, HTASK task, BOOL old)
{
    WORD *c = chain(type);
    PHOOK h;
    if (!c || !proc)
        return 0;
    h = (PHOOK)LocalAlloc(LPTR, sizeof(HOOK));
    if (!h)
        return 0;
    h->type = type;
    h->proc = proc;
    h->hinst = hinst;
    h->task = task;
    h->old = old;
    h->next = *c;
    *c = (WORD)h;
    return h;
}

HHOOK WINAPI SetWindowsHookEx(int type, HOOKPROC proc, HINSTANCE hinst, HTASK task)
{
    PHOOK h = add_hook(type, proc, hinst, task, FALSE);
    return h ? (HHOOK)MAKELONG((WORD)h, 0x4B48) : 0;    /* "HK" */
}

/* the 3.0 interface: returns the previous hook procedure (3.1 SDK), which
   the caller passes to DefHookProc */
HOOKPROC WINAPI SetWindowsHook(int type, HOOKPROC proc)
{
    WORD *c = chain(type);
    PHOOK prev = c && *c ? (PHOOK)*c : (PHOOK)0;
    ulog("SetWindowsHook\r\n");  /* TRACE */
    if (!add_hook(type, proc, 0, 0, TRUE))
        return 0;
    return prev ? prev->proc : 0;
}

static BOOL unhook(PHOOK h)
{
    WORD *c = chain(h->type), __far *l;
    if (!c)
        return FALSE;
    for (l = c; *l; l = &((PHOOK)*l)->next)
        if (*l == (WORD)h) {
            *l = h->next;
            LocalFree((HLOCAL)h);
            return TRUE;
        }
    return FALSE;
}

BOOL WINAPI UnhookWindowsHookEx(HHOOK hh)
{
    return HIWORD((DWORD)hh) == 0x4B48 && unhook((PHOOK)LOWORD((DWORD)hh));
}

BOOL WINAPI UnhookWindowsHook(int type, HOOKPROC proc)
{
    WORD *c = chain(type);
    PHOOK h;
    if (!c)
        return FALSE;
    for (h = (PHOOK)*c; h; h = (PHOOK)h->next)
        if (h->proc == proc)
            return unhook(h);
    return FALSE;
}

static LRESULT call_from(PHOOK h, int code, WPARAM wp, LPARAM lp)
{
    HTASK me = GetCurrentTask();
    for (; h; h = (PHOOK)h->next)
        if (!h->task || h->task == me)
            return ((LRESULT (__far __pascal *)(int, WPARAM, LPARAM))h->proc)(code, wp, lp);
    return 0;
}

LRESULT WINAPI CallNextHookEx(HHOOK hh, int code, WPARAM wp, LPARAM lp)
{
    PHOOK h;
    if (HIWORD((DWORD)hh) != 0x4B48)
        return 0;
    h = (PHOOK)LOWORD((DWORD)hh);
    return call_from((PHOOK)h->next, code, wp, lp);
}

/* the 3.0 interface: next is the HOOKPROC far pointer stored by the
   caller (lpfnNext: the address of that variable) */
LRESULT WINAPI DefHookProc(int code, WPARAM wp, LPARAM lp, HOOKPROC __far *next)
{
    int t;
    if (!next || !*next)
        return 0;
    for (t = 0; t < NHOOKTYPES; t++) {
        PHOOK h;
        for (h = (PHOOK)chains[t]; h; h = (PHOOK)h->next)
            if (h->proc == *next)
                return ((LRESULT (__far __pascal *)(int, WPARAM, LPARAM))h->proc)(code, wp, lp);
    }
    return 0;
}

LRESULT call_hook(int type, int code, WPARAM wp, LPARAM lp)
{
    WORD *c = chain(type);
    return c && *c ? call_from((PHOOK)*c, code, wp, lp) : 0;
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
