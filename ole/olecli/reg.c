/*
 * reg.c - the registration database as OLECLI reads it (SHELL's Reg API,
 * documented in the Windows 3.1 SDK): a class's server is
 * HKEY_CLASSES_ROOT\<class>\protocol\<protocol>\server (REG.DAT, as
 * SETUP.REG writes it); WIN.INI's [embedding] section
 * ("class=description,description,server,format") is the older place
 * (UNVERIFIED that OLECLI still reads it when the database has no entry).
 */
#include "olecli.h"
#include <shellapi.h>

static BOOL query(LPCSTR key, LPSTR out, WORD cb)
{
    LONG n = cb;
    out[0] = 0;
    if (RegQueryValue(HKEY_CLASSES_ROOT, key, out, &n) != ERROR_SUCCESS)
        return FALSE;
    return out[0] != 0;
}

BOOL reg_server(LPCSTR cls, LPCSTR protocol, LPSTR out, WORD cb)
{
    char key[300];
    if (!cls || !*cls || slen(cls) + slen(protocol) + 30 > sizeof key)
        return FALSE;
    lstrcpy(key, cls);
    lstrcat(key, "\\protocol\\");
    lstrcat(key, protocol);
    lstrcat(key, "\\server");
    if (query(key, out, cb))
        return TRUE;
    if (!lstrcmp(protocol, "StdFileEditing")) {
        /* [embedding] class=description,description,server,format */
        char line[300];
        WORD i = 0, commas = 0, j = 0;
        if (!GetProfileString("embedding", cls, "", line, sizeof line))
            return FALSE;
        while (line[i] && commas < 2)
            if (line[i++] == ',')
                commas++;
        while (line[i] && line[i] != ',' && j + 1 < cb)
            out[j++] = line[i++];
        out[j] = 0;
        return j != 0;
    }
    return FALSE;
}

BOOL reg_class_known(LPCSTR cls)
{
    char path[260];
    return reg_server(cls, "StdFileEditing", path, sizeof path);
}

/* a file's class by its extension (".bmp" -> "PBrush") */
BOOL reg_class_of_file(LPCSTR file, LPSTR cls, WORD cb)
{
    WORD i, dot = 0xFFFF;
    for (i = 0; file[i]; i++)
        if (file[i] == '.')
            dot = i;
        else if (file[i] == '\\' || file[i] == ':')
            dot = 0xFFFF;
    if (dot == 0xFFFF)
        return FALSE;
    return query(file + dot, cls, cb);
}
