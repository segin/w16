/*
 * sh3.c - SH3: SHELL's drag-and-drop half: DragAcceptFiles on a window
 * (style bits, window procedure, properties) and DragQueryFile,
 * DragQueryPoint and DragFinish on drop blocks the probe builds itself in
 * the layout Schulman et al. describe (Undocumented Windows, ch. 6,
 * DROPINFO: WORD offset of the first name, x, y, BOOL in client area, then
 * NUL-terminated names ending with an empty one), to confirm that layout
 * and the functions' results. What File Manager actually sends is seen
 * by the scenario test (tests/shellscen.sh, SH9 as the drop target).
 */
#include <windows.h>

#define TK_REF "sh3ref.h"
#include "tklog.h"
#include "shprobe.h"

static char buf[300];
static WORD nprops;

BOOL CALLBACK __export PropProc(HWND w, LPCSTR name, HANDLE h)
{
    (void)w;
    (void)h;
    nprops++;
    if (HIWORD((DWORD)name))
        obs_str("prop", name);
    else
        obs("prop.atom", LOWORD((DWORD)name));
    return TRUE;
}

/* a drop block: header {ofs, x, y, client} at offset 0, names at ofs */
static HGLOBAL make_drop(WORD ofs, int x, int y, WORD client, const char *names, WORD nlen,
                         UINT flags)
{
    HGLOBAL h = GlobalAlloc(flags, ofs + nlen);
    BYTE FAR *p = (BYTE FAR *)GlobalLock(h);
    WORD i;
    for (i = 0; i < ofs; i++)
        p[i] = 0xEE;
    ((WORD FAR *)p)[0] = ofs;
    ((WORD FAR *)p)[1] = (WORD)x;
    ((WORD FAR *)p)[2] = (WORD)y;
    ((WORD FAR *)p)[3] = client;
    for (i = 0; i < nlen; i++)
        p[ofs + i] = names[i];
    GlobalUnlock(h);
    return h;
}

static const char names[] = "C:\\WINDOWS\\NOTEPAD.EXE\0C:\\AUTOEXEC.BAT\0x\0";

static void query_all(const char *tag, HDROP h)
{
    char n[64];
    UINT r, i, cnt;
    POINT pt;
    cnt = DragQueryFile(h, (UINT)-1, NULL, 0);
    suffix(n, tag, ".count");
    obs(n, cnt);
    for (i = 0; i < cnt + 1 && i < 6; i++) {
        r = DragQueryFile(h, i, NULL, 0);
        obsn(tag, i, r);
        lstrcpy(buf, "XXXXXXXX");
        r = DragQueryFile(h, i, buf, sizeof buf);
        obsn(tag, 0x100 + i, r);
        suffix(n, tag, ".name0");
        n[lstrlen(n) - 1] = (char)('0' + i);
        obs_s(n, buf);
    }
    lstrcpy(buf, "XXXXXXXXXXXX");
    r = DragQueryFile(h, 0, buf, 5);
    suffix(n, tag, ".cb5");
    obs(n, r);
    suffix(n, tag, ".cb5.text");
    obs_s(n, buf);
    lstrcpy(buf, "XXXXXXXXXXXX");
    r = DragQueryFile(h, 0, buf, 0);
    suffix(n, tag, ".cb0");
    obs(n, r);
    suffix(n, tag, ".cb0.text");
    obs_s(n, buf);
    r = DragQueryFile(h, 0xFFFE, buf, sizeof buf);
    suffix(n, tag, ".fffe");
    obs(n, r);
    r = DragQueryFile(h, 0x8000, buf, sizeof buf);
    suffix(n, tag, ".8000");
    obs(n, r);
    pt.x = pt.y = 0x5555;
    r = DragQueryPoint(h, &pt);
    suffix(n, tag, ".pt");
    obs(n, r);
    suffix(n, tag, ".pt.x");
    obs(n, pt.x);
    suffix(n, tag, ".pt.y");
    obs(n, pt.y);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HWND w;
    HINSTANCE inst = (HINSTANCE)init_regs[5];
    HGLOBAL h;
    FARPROC pp;
    LONG proc;

    WaitEvent(0);
    InitApp(inst);
    out("SH3 drag and drop\r\n");
    wc.style = 0;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = inst;
    wc.lpszClassName = "SH3";
    RegisterClass(&wc);
    w = CreateWindow("SH3", "SH3", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, NULL, NULL, inst, NULL);
    pp = MakeProcInstance((FARPROC)PropProc, inst);

    proc = GetWindowLong(w, GWL_WNDPROC);
    obs("ex.before", LOWORD(GetWindowLong(w, GWL_EXSTYLE)));
    DragAcceptFiles(w, TRUE);
    obs("ex.on", LOWORD(GetWindowLong(w, GWL_EXSTYLE)));
    obs("ex.on.hi", HIWORD(GetWindowLong(w, GWL_EXSTYLE)));
    obs("style.on.hi", HIWORD(GetWindowLong(w, GWL_STYLE)));
    obs("proc.same", GetWindowLong(w, GWL_WNDPROC) == proc);
    EnumProps(w, (PROPENUMPROC)pp);
    obs("props.on", nprops);
    DragAcceptFiles(w, 5);
    obs("ex.5", LOWORD(GetWindowLong(w, GWL_EXSTYLE)));
    DragAcceptFiles(w, FALSE);
    obs("ex.off", LOWORD(GetWindowLong(w, GWL_EXSTYLE)));
    SetWindowLong(w, GWL_EXSTYLE, 0x0001);
    DragAcceptFiles(w, TRUE);
    obs("ex.keep", LOWORD(GetWindowLong(w, GWL_EXSTYLE)));
    DragAcceptFiles(w, FALSE);
    obs("ex.keep.off", LOWORD(GetWindowLong(w, GWL_EXSTYLE)));
    DragAcceptFiles(NULL, TRUE);
    out("null window ok\r\n");

    h = make_drop(8, 12, 34, 1, names, sizeof names, GMEM_MOVEABLE | GMEM_SHARE);
    query_all("d8", (HDROP)h);
    obs("d8.flags.before", GlobalFlags(h));
    DragFinish((HDROP)h);
    obs("d8.flags.after", GlobalFlags(h));
    obs("d8.size.after", (WORD)GlobalSize(h));

    h = make_drop(14, -5, 7, 0, names, sizeof names, GMEM_MOVEABLE | GMEM_SHARE);
    query_all("d14", (HDROP)h);
    DragFinish((HDROP)h);

    h = make_drop(8, 1, 2, 2, "\0", 2, GMEM_MOVEABLE);
    query_all("dempty", (HDROP)h);
    DragFinish((HDROP)h);

    h = make_drop(8, 1, 2, 1, names, sizeof names, GMEM_FIXED);
    query_all("dfixed", (HDROP)h);
    obs("dfixed.lock", GlobalFlags(h));
    DragFinish((HDROP)h);
    obs("dfixed.after", GlobalFlags(h));

    /* a locked block stays locked? */
    h = make_drop(8, 1, 2, 1, names, sizeof names, GMEM_MOVEABLE);
    GlobalLock(h);
    DragQueryFile((HDROP)h, 0, buf, sizeof buf);
    obs("dlocked.flags", GlobalFlags(h));
    GlobalUnlock(h);
    DragFinish((HDROP)h);

    DestroyWindow(w);
    out("SH3 done\r\n");
    return failures;
}
