/*
 * ver.h - w16's VER.DLL, internal definitions (ARCHITECTURE.md, VER).
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 2 (the Ver*
 * functions, GetFileResource*, their flags and results), Vol. 3
 * (VS_FIXEDFILEINFO), Vol. 4 (the VERSIONINFO resource's layout); the
 * values not given there were observed with tests/ver/vrp.c.
 */
#ifndef W16_VER_H
#define W16_VER_H

#include <windows.h>
#include <lzexpand.h>

#define VAPI __loadds FAR PASCAL        /* exported by ver.lnk */

/* VerFindFile (SDK Vol. 2) */
#define VFFF_ISSHAREDFILE   0x0001
#define VFF_CURNEDEST       0x0001
#define VFF_FILEINUSE       0x0002
#define VFF_BUFFTOOSMALL    0x0004

/* VerInstallFile (SDK Vol. 2) */
#define VIFF_FORCEINSTALL   0x0001
#define VIFF_DONTDELETEOLD  0x0002
#define VIF_TEMPFILE         0x00000001L
#define VIF_MISMATCH         0x00000002L
#define VIF_SRCOLD           0x00000004L
#define VIF_DIFFLANG         0x00000008L
#define VIF_DIFFCODEPG       0x00000010L
#define VIF_DIFFTYPE         0x00000020L
#define VIF_WRITEPROT        0x00000040L
#define VIF_FILEINUSE        0x00000080L
#define VIF_OUTOFSPACE       0x00000100L
#define VIF_ACCESSVIOLATION  0x00000200L
#define VIF_SHARINGVIOLATION 0x00000400L
#define VIF_CANNOTCREATE     0x00000800L
#define VIF_CANNOTDELETE     0x00001000L
#define VIF_CANNOTRENAME     0x00002000L
#define VIF_CANNOTDELETECUR  0x00004000L
#define VIF_OUTOFMEMORY      0x00008000L
#define VIF_CANNOTREADSRC    0x00010000L
#define VIF_CANNOTREADDST    0x00020000L
#define VIF_BUFFTOOSMALL     0x00040000L

/* the version resource: type 16, name 1 (SDK Vol. 4: VS_FILE_INFO,
   VS_VERSION_INFO); its fixed part's signature (Vol. 3) */
#define RT_VER       16
#define VER_ID       1
#define VS_SIG       0xFEEF04BDL
#define VS_SIGOFF    20                 /* observed: checked here only */

typedef struct {
    DWORD dwSignature, dwStrucVersion;
    DWORD dwFileVersionMS, dwFileVersionLS;
    DWORD dwProductVersionMS, dwProductVersionLS;
    DWORD dwFileFlagsMask, dwFileFlags;
    DWORD dwFileOS, dwFileType, dwFileSubtype;
    DWORD dwFileDateMS, dwFileDateLS;
} FIXEDINFO;

#define PATHMAX 144

#endif
