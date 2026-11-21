/* cplutil.h - helpers shared by w16's applet DLLs (cplutil.c) */
#ifndef W16_CPLUTIL_H
#define W16_CPLUTIL_H

/* a DOS find (INT 21h 4Eh/4Fh) with a DTA of our own; the caller's DTA is
   put back after each call */
typedef struct {
    char reserved[21];
    char attr;
    WORD time, date;
    DWORD size;
    char name[13];
} CPLFIND;
BOOL cpl_find(const char FAR *spec, WORD attr, CPLFIND FAR *f, BOOL first);

/* the Windows and System directories with a trailing backslash; the
   length (0: does not fit) */
int cpl_windir(char *buf, int size);
int cpl_sysdir(char *buf, int size);

/* "path\name" into buf (size bytes), a backslash added when needed */
void cpl_join(char *buf, int size, const char *dir, const char *name);

/* the text of an NE module's description (its non-resident name table's
   first entry); FALSE if the file is not an NE file */
BOOL cpl_ne_desc(const char *path, char *buf, int size);

/* bytes of a file; -1 if it cannot be opened */
long cpl_file_size(const char *path);

/* a copy of src as dst; FALSE on any error (dst removed) */
BOOL cpl_copy_file(const char *src, const char *dst);

/* delete a file (INT 21h 41h); TRUE when gone */
BOOL cpl_delete(const char *path);

/* n with WIN.INI [intl] sThousand between groups of three digits */
void cpl_thousands(char *buf, unsigned long n);

/* all keys of a WIN.INI (ini NULL) or private profile section, as
   GetProfileString gives them with a NULL key (each 0-terminated, a 0
   after the last), in a global block that grows until they fit; NULL
   when out of memory. GlobalFree the block. */
HGLOBAL cpl_section_keys(const char *ini, const char *section);

/* a decimal number of s (leading blanks, a sign); FALSE if none or
   something follows it */
BOOL cpl_atol(const char *s, long *v);

#endif
