/*
 * kfile.h - KERNEL internals shared by file.c (OpenFile, _l*, _h*, temp
 * files) and profile.c (.INI files).
 */
#ifndef W16_KFILE_H
#define W16_KFILE_H

#include "kernel.h"

#define HFILE_ERROR     0xFFFF

/* DOS errors (INT 21h) that the APIs report */
#define DOSE_NOFILE     2
#define DOSE_NOPATH     3

/* file.c */
int  dos_call(regs16 *r);                       /* INT 21h; 1 = ok */
u16  dos_open(const char FAR *path, u16 mode, u16 *err);  /* FFFF on error */
u16  dos_create(const char FAR *path, u16 attr, u16 *err);
int  dos_close(u16 fh);
u32  dos_seek(u16 fh, u32 pos, u16 how);        /* FFFFFFFF on error */
u32  dos_rw(u16 ah, u16 fh, u16 sel, u16 off, u32 n); /* FFFFFFFF on error */
int  file_canon(const char FAR *name, char *out, u16 *err);
int  file_has_path(const char FAR *name);
const char FAR *file_env(const char *name);     /* task environment */
u16  KAPI_ES GetWindowsDirectory(char FAR *buf, u16 n);
u16  KAPI_ES GetSystemDirectory(char FAR *buf, u16 n);

/* huge buffers: byte off of the block starting at selector sel */
u8 FAR *huge_at(u16 sel, u32 off);
#define HUGE_AT(sel, off) huge_at((sel), (off))

#endif
