/*
 * osspy.h - the OLE/DDE spy DLL (OSSPY.DLL) the OLESVR probes share.
 *
 * OsSpyStart installs system-wide WH_GETMESSAGE and WH_CALLWNDPROC hooks
 * and logs every WM_DDE_* message any window receives, decoded (atoms by
 * name, data handles by format and content), to C:\OS.LOG. The probes
 * write their own lines (vtable callbacks, API results) to the same file
 * through OsPrintf, so one log holds both sides in the order they ran.
 *
 * OsPrintf understands %s (far string), %q (far string, quoted and
 * escaped), %d, %u, %x (4 hex digits), %ld, %lx (8 hex digits), %c,
 * %a (a global atom by name), %w (a window as MODULE:Class#n, numbered in
 * the order windows first appear, so logs of two runs can be compared).
 */
#ifndef OSSPY_H
#define OSSPY_H

void FAR PASCAL OsSpyStart(void);
void FAR PASCAL OsSpyStop(void);
void FAR __cdecl OsPrintf(const char FAR *fmt, ...);
/* log the contents of a global block as a DDE value of format cf */
void FAR PASCAL OsDumpData(WORD cf, HGLOBAL h, WORD offset);
/* 0: log only; 1: echo to nothing else (reserved) */
void FAR PASCAL OsSetQuiet(BOOL quiet);
/* TRUE: log every sent WM_DDE_* message too (default: INITIATE and ACK) */
void FAR PASCAL OsSpySent(BOOL on);

#endif
