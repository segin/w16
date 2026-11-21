/*
 * hook.c - window hooks (SetWindowsHook, SetWindowsHookEx, CallNextHookEx,
 * DefHookProc, UnhookWindowsHook(Ex), CallMsgFilter).
 *
 * Each hook type (WH_MSGFILTER -1 .. WH_SHELL 10) has a chain, newest
 * first; a hook is task-local when installed with a task, else system
 * wide. w16 calls WH_MSGFILTER/WH_SYSMSGFILTER from CallMsgFilter, and
 * WH_SHELL with HSHELL_WINDOWCREATED/DESTROYED/ACTIVATESHELLWINDOW (3.1
 * SDK), WH_JOURNALPLAYBACK from input.c, WH_GETMESSAGE from GetMessage
 * and PeekMessage and WH_CALLWNDPROC from SendMessage (msg.c); the other
 * types are kept but not yet called (UNVERIFIED which places Windows 3.1
 * calls them from).
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
    HQUEUE hq;                  /* the installing task's queue */
    HMODULE module;             /* where the procedure is (hinst's module) */
    WORD   busy;                /* calls of the procedure running */
    WORD   dead;                /* unhooked while busy: freed after */
} HOOK;
typedef HOOK __far *PHOOK;
#define HK(h)   ((PHOOK)UH_PTR(&uhObj, (h)))

static WORD chains[NHOOKTYPES];
BOOL hook_set(int type);

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
    /* WH_MSGFILTER is task-specific whatever the call says (3.1 SDK,
       SetWindowsHook: "the only exception is WH_MSGFILTER, which is
       task-specific"; WH_SYSMSGFILTER is the system-wide one). Kept
       system-wide, Program Manager's F1 filter took Notepad's menu and
       dialog messages once USER's loops called CallMsgFilter (boot tests
       helpmenus, tabs, runfront and more) */
    if (type == WH_MSGFILTER && !task)
        task = GetCurrentTask();
    h->type = type;
    h->proc = proc;
    h->hinst = hinst;
    h->task = task;
    h->old = old;
    h->hq = cur_hq();
    h->module = K_GETEXEPTR(hinst ? (HANDLE)hinst : (HANDLE)SELECTOROF(proc));
    h->next = *c;
    *c = hh;
    return hh;
}

HHOOK WINAPI SetWindowsHookEx(int type, HOOKPROC proc, HINSTANCE hinst, HTASK task)
{
    WORD h = add_hook(type, proc, hinst, task, FALSE);
    return h ? (HHOOK)MAKELONG(h, 0x4B48) : 0;          /* "HK" */
}

/* the first live hook from h on */
static WORD live(WORD h)
{
    while (h && HK(h)->dead)
        h = HK(h)->next;
    return h;
}

/* the 3.0 interface: returns the previous hook procedure (3.1 SDK), which
   the caller passes to DefHookProc */
HOOKPROC WINAPI SetWindowsHook(int type, HOOKPROC proc)
{
    WORD *c = chain(type), first = c ? live(*c) : 0;
    HOOKPROC prev = first ? HK(first)->proc : 0;
    if (!add_hook(type, proc, 0, 0, TRUE))
        return 0;
    return prev;
}

/* h out of its chain and freed */
static void free_hook(WORD h)
{
    WORD *c = chain(HK(h)->type), __far *l;
    for (l = c; *l; l = &HK(*l)->next)
        if (*l == h) {
            *l = HK(h)->next;
            break;
        }
    uh_free(&uhObj, h);
}

/* a hook block of ours: in the heap, its type's chain has it. One whose
   procedure is running (it unhooks itself, then calls CallNextHookEx with
   its own handle, as programs do) stays in the chain, passed over, until
   that call returns: freed at once, its next link was read from a freed
   block (USER audit, item 6) */
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
            if (HK(h)->dead)
                return FALSE;
            if (HK(h)->busy)
                HK(h)->dead = 1;
            else
                free_hook(h);
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
    for (h = live(*c); h; h = live(HK(h)->next))
        if (HK(h)->proc == proc)
            return unhook(h);
    return FALSE;
}

/* the procedure of hook h; h is held (busy) during the call */
static LRESULT call_one(WORD h, int code, WPARAM wp, LPARAM lp)
{
    HOOKPROC f = HK(h)->proc;
    LRESULT r;
    HK(h)->busy++;
    r = ((LRESULT (__far __pascal *)(int, WPARAM, LPARAM))f)(code, wp, lp);
    if (!--HK(h)->busy && HK(h)->dead)
        free_hook(h);
    return r;
}

static LRESULT call_from(WORD h, int code, WPARAM wp, LPARAM lp)
{
    HTASK me = GetCurrentTask();
    for (; h; h = HK(h)->next)
        if (!HK(h)->dead && (!HK(h)->task || HK(h)->task == me))
            return call_one(h, code, wp, lp);
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
        for (h = live(chains[t]); h; h = live(HK(h)->next))
            if (HK(h)->proc == *next)
                return call_one(h, code, wp, lp);
    }
    return 0;
}

LRESULT call_hook(int type, int code, WPARAM wp, LPARAM lp)
{
    WORD *c = chain(type);
    return c && *c ? call_from(*c, code, wp, lp) : 0;
}

/* Hooks that outlive what they belong to go (Pietrek, Windows Internals
   ch. 3: AppExit's UnhookHooks, and ModuleUnload's for the module when
   its last instance ends or a task faults). US29 hk on 3.1: a
   system-wide hook in a DLL, set by a task that then ended, is still
   called while the DLL stays loaded; so a task's end takes the hooks for
   that task alone, and a module's end the hooks whose procedure is in
   it. w16 kept them all: a system-wide hook in a DLL that was freed left
   its procedure dangling (USER audit, item 6). */
static void unhook_if(int what, WORD v)
{
    int t;
    for (t = 0; t < NHOOKTYPES; t++) {
        WORD h, n;
        for (h = chains[t]; h; h = n) {
            PHOOK k = HK(h);
            n = k->next;
            if (k->dead)
                continue;
            if ((what == 0 && k->task == (HTASK)v) ||
                (what == 1 && k->module == (HMODULE)v))
                unhook(h);
        }
    }
}

void hooks_queue_gone(HQUEUE hq, HTASK task)
{
    (void)hq;
    if (task)
        unhook_if(0, (WORD)task);
}

void hooks_module_gone(HMODULE m)
{
    if (m)
        unhook_if(1, (WORD)m);
}

/* a bad MSG: FALSE (3.1, US27) */
BOOL WINAPI CallMsgFilter(MSG __far *m, int code)
{
    if (GP_BAD(m))
        return FALSE;
    /* the answer is a BOOL: AX alone (Program Manager's filter returns
       0 in AX and whatever was in DX; taken as a DWORD, its "processed"
       ate the keys of every menu and dialog: boot tests runfront, tabs,
       helpmenus, ...) */
    if (LOWORD(call_hook(WH_SYSMSGFILTER, code, 0, (LPARAM)m)))
        return TRUE;
    return LOWORD(call_hook(WH_MSGFILTER, code, 0, (LPARAM)m)) != 0;
}

/* WH_CBT (US30 cbt): nonzero refuses what code announces (these
   answers are BOOLs: AX alone, as CallMsgFilter's) */
LRESULT cbt_hook(int code, WPARAM wp, LPARAM lp)
{
    return hook_set(WH_CBT) ? LOWORD(call_hook(WH_CBT, code, wp, lp)) : 0;
}

/* WH_KEYBOARD, WH_MOUSE (US30 kbd, mouse): HC_ACTION as the message is
   taken from the system queue (nonzero: thrown away), HC_NOREMOVE at a
   PM_NOREMOVE look */
LRESULT kbd_hook(int code, WPARAM vk, LPARAM data)
{
    LRESULT r = hook_set(WH_KEYBOARD) ? LOWORD(call_hook(WH_KEYBOARD, code, vk, data)) : 0;
    /* a key thrown away: WH_CBT's HCBT_KEYSKIPPED with it (US30 skip on
       3.1: C7 with the virtual key for the press and the release a
       refusing WH_KEYBOARD threw away; none for keys taken normally) */
    if (r && code == HC_ACTION)
        cbt_hook(HCBT_KEYSKIPPED, vk, data);
    return r;
}

LRESULT mouse_hook(int code, UINT msg, POINT pt, HWND h, int hit)
{
    MOUSEHOOKSTRUCT ms;
    if (!hook_set(WH_MOUSE))
        return 0;
    ms.pt = pt;
    ms.hwnd = h;
    ms.wHitTestCode = hit;
    ms.dwExtraInfo = 0;
    if (!LOWORD(call_hook(WH_MOUSE, code, msg, (LPARAM)(MOUSEHOOKSTRUCT __far *)&ms)))
        return 0;
    /* a mouse message thrown away: HCBT_CLICKSKIPPED with it (US30 skip
       on 3.1: C6 201, C6 202; lParam the MOUSEHOOKSTRUCT, 3.1 SDK) */
    if (code == HC_ACTION)
        cbt_hook(HCBT_CLICKSKIPPED, msg, (LPARAM)(MOUSEHOOKSTRUCT __far *)&ms);
    return 1;
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
    return c && live(*c);
}
