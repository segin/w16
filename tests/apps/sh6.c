/*
 * sh6.c - SH6: SHELL's icon extraction: ExtractIcon (counts, indices,
 * kinds of file, the icon block it makes), ExtractAssociatedIcon (what it
 * does to the path and index it is given) and InternalExtractIcon (what
 * Program Manager's Change Icon uses). Each icon is logged as the 12-byte
 * header of its block and a checksum of the whole block (the block's
 * layout: ARCHITECTURE.md 6.11). tests/shellprobe.sh SH6 provides the
 * files it reads (tools/mkshfiles.py).
 */
#include <windows.h>

#define TK_REF "sh6ref.h"
#include "tklog.h"
#include "shprobe.h"

static char path[200];
static HINSTANCE inst;

static void icon(const char *name, HICON h)
{
    char n[64];
    WORD FAR *p;
    WORD i, size;
    if ((UINT)h <= 1) {
        obs(name, (WORD)h);
        return;
    }
    obs(name, 2);                       /* a handle */
    size = (WORD)GlobalSize((HGLOBAL)h);
    suffix(n, name, ".size");
    obs(n, size);
    p = (WORD FAR *)GlobalLock((HGLOBAL)h);
    if (!p)
        return;
    for (i = 0; i < 6; i++) {
        suffix(n, name, ".h0");
        n[lstrlen(n) - 1] = (char)('0' + i);
        obs(n, p[i]);
    }
    suffix(n, name, ".sum");
    obs(n, strsum((const char FAR *)p, size));
    GlobalUnlock((HGLOBAL)h);
}

static void file(const char *tag, LPCSTR f)
{
    char n[64];
    UINT cnt, i;
    HICON h;
    cnt = (UINT)ExtractIcon(inst, f, (UINT)-1);
    suffix(n, tag, ".count");
    obs(n, cnt);
    for (i = 0; i < 3 && i <= cnt; i++) {
        h = ExtractIcon(inst, f, i);
        suffix(n, tag, ".0");
        n[lstrlen(n) - 1] = (char)('0' + i);
        icon(n, h);
        if ((UINT)h > 1)
            DestroyIcon(h);
    }
    if (cnt > 3) {
        h = ExtractIcon(inst, f, cnt - 1);
        suffix(n, tag, ".last");
        icon(n, h);
        if ((UINT)h > 1)
            DestroyIcon(h);
        h = ExtractIcon(inst, f, cnt);
        suffix(n, tag, ".past");
        icon(n, h);
    }
    h = ExtractIcon(inst, f, 0xFFFE);
    suffix(n, tag, ".fffe");
    icon(n, h);
}

static void assoc(const char *tag, LPCSTR f, WORD idx)
{
    char n[64];
    WORD w = idx;
    HICON h;
    lstrcpy(path, f);
    h = ExtractAssociatedIcon(inst, path, &w);
    icon(tag, h);
    suffix(n, tag, ".path");
    obs_s(n, path);
    suffix(n, tag, ".idx");
    obs(n, w);
    if ((UINT)h > 1)
        DestroyIcon(h);
}

static void internal(const char *tag, LPCSTR f, UINT first, UINT cnt)
{
    char n[64];
    HGLOBAL h = (HGLOBAL)InternalExtractIcon(inst, f, first, cnt);
    WORD FAR *p;
    WORD i, size;
    obs(tag, h ? 1 : 0);
    if (!h)
        return;
    size = (WORD)GlobalSize(h);
    suffix(n, tag, ".size");
    obs(n, size);
    suffix(n, tag, ".flags");
    obs(n, GlobalFlags(h));
    p = (WORD FAR *)GlobalLock(h);
    if (!p)
        return;
    for (i = 0; i < size / 2 && i < 8; i++) {
        suffix(n, tag, ".e0");
        n[lstrlen(n) - 1] = (char)('0' + i);
        if ((UINT)p[i] > 1) {
            icon(n, (HICON)p[i]);
        } else {
            obs(n, p[i]);
        }
    }
    GlobalUnlock(h);
}

int __cdecl tmain(void)
{
    inst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(inst);
    out("SH6 icons\r\n");
    file("progman", "C:\\WINDOWS\\PROGMAN.EXE");
    file("moricons", "C:\\WINDOWS\\MORICONS.DLL");
    file("notepad", "NOTEPAD.EXE");
    file("winfile", "C:\\WINDOWS\\WINFILE.EXE");
    file("control", "C:\\WINDOWS\\CONTROL.EXE");
    file("self", "C:\\SH6.EXE");
    file("ico", "C:\\SHI.ICO");
    file("ico2", "C:\\SHI2.ICO");
    file("winini", "C:\\WINDOWS\\WIN.INI");
    file("com", "C:\\SHD.COM");
    file("mz", "C:\\SHMZ.EXE");
    file("missing", "C:\\NONE.EXE");
    file("dir", "C:\\WINDOWS");
    file("noext", "C:\\WINDOWS\\PROGMAN");
    file("lower", "c:\\windows\\progman.exe");
    file("shell", "C:\\WINDOWS\\SYSTEM\\SHELL.DLL");
    file("user", "C:\\WINDOWS\\SYSTEM\\USER.EXE");

    assoc("a.txt", "C:\\WINDOWS\\README.WRI", 0);
    assoc("a.ini", "C:\\WINDOWS\\WIN.INI", 0);
    assoc("a.exe", "C:\\WINDOWS\\PROGMAN.EXE", 2);
    assoc("a.exe.big", "C:\\WINDOWS\\PROGMAN.EXE", 200);
    assoc("a.self", "C:\\SH6.EXE", 0);
    assoc("a.xyz", "C:\\SHX.XYZ", 0);
    assoc("a.missing", "C:\\NONE.TXT", 0);
    assoc("a.missing.xyz", "C:\\NONE.XYZ", 0);
    assoc("a.com", "C:\\SHD.COM", 0);
    assoc("a.mz", "C:\\SHMZ.EXE", 0);
    assoc("a.ico", "C:\\SHI.ICO", 0);
    assoc("a.rel", "NOTEPAD.EXE", 0);
    assoc("a.bmp", "C:\\WINDOWS\\ARCADE.BMP", 0);

    internal("i.progman", "C:\\WINDOWS\\PROGMAN.EXE", 0, 3);
    internal("i.progman.all", "C:\\WINDOWS\\PROGMAN.EXE", 0, 100);
    internal("i.progman.mid", "C:\\WINDOWS\\PROGMAN.EXE", 5, 2);
    internal("i.progman.0", "C:\\WINDOWS\\PROGMAN.EXE", 0, 0);
    internal("i.self", "C:\\SH6.EXE", 0, 3);
    internal("i.ico", "C:\\SHI.ICO", 0, 3);
    internal("i.missing", "C:\\NONE.EXE", 0, 3);
    internal("i.winini", "C:\\WINDOWS\\WIN.INI", 0, 3);
    out("SH6 done\r\n");
    return failures;
}
