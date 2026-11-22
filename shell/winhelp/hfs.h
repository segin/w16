/*
 * hfs.h - w16 Windows Help: an in-memory help file system that can be
 * written (WINHELP.BMK, .ANN files; hfs.c). Portable, as hlp.h.
 */
#ifndef W16_HFS_H
#define W16_HFS_H
#include "hlp.h"

#define HFS_MAXFILES 128

typedef struct {
    LPB buf;
    DWORD size, cap;
    WORD pagesize;          /* the directory's B+ tree pages */
    DWORD dirstart, freelist;
    WORD n;
    char name[HFS_MAXFILES][32];
    DWORD off[HFS_MAXFILES];
} HFS;

BOOL hfs_new(HFS FAR *f, WORD pagesize);
BOOL hfs_load(HFS FAR *f, LPCB data, DWORD n);
void hfs_free(HFS FAR *f);
LPB hfs_get(HFS FAR *f, const char FAR *name, DWORD FAR *len);
BOOL hfs_put(HFS FAR *f, const char FAR *name, LPCB data, DWORD n);
BOOL hfs_del(HFS FAR *f, const char FAR *name);

#endif
