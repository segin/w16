/*
 * hfstest.c - host test of w16 WINHELP's help file system writer
 * (shell/winhelp/hfs.c): replays what 3.1's WINHELP was seen to do and
 * writes the files, to compare with the ones 3.1 left (tests/whobs.sh
 * WH3 and WH4 bring them back; they are not kept in the repository):
 *   hfstest ann0 OUT    Annotate opened on a file without annotations:
 *                       @LINK (no annotations) and @VERSION
 *   hfstest ann1 OUT    then "A note." saved for TOPICPOS 4Eh
 *   hfstest bmk1 OUT    WINHELP.BMK with NOTEPAD.HLP's bookmark "Mark one"
 *   hfstest bmk2 OUT    then a second bookmark "Third" (TOPICPOS 2D0h)
 * Build: cc -DHLP_HOST -Ishell/winhelp -o build/hfstest tests/winhelp/hfstest.c
 *        shell/winhelp/hfs.c shell/winhelp/hlp.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hfs.h"

LONG hio_open(const char *path) { (void)path; return -1; }
void hio_close(LONG fh) { (void)fh; }
BOOL hio_read(LONG fh, DWORD off, void *buf, WORD n) { (void)fh; (void)off; (void)buf; (void)n; return FALSE; }
void *hio_alloc(DWORD n) { return calloc(1, n ? n : 1); }
void hio_free(void *p) { free(p); }

static const BYTE version[6] = { 0x08, 0x62, 0x6D, 0x66, 0x01, 0x00 };

static int bookmarks(HFS *f, int n)
{
    /* minor, date, count, size; then per bookmark TOPICPOS, 0, name */
    static const struct { DWORD pos; const char *name; } bm[] = { { 0x4E, "Mark one" }, { 0x2D0, "Third" } };
    BYTE b[256];
    int i, k = 10;
    for (i = 0; i < n; i++) {
        memset(b + k, 0, 8);
        b[k] = (BYTE)bm[i].pos;
        b[k + 1] = (BYTE)(bm[i].pos >> 8);
        strcpy((char *)b + k + 8, bm[i].name);
        k += 8 + strlen(bm[i].name) + 1;
    }
    b[0] = 0x15; b[1] = 0;
    b[2] = 0x84; b[3] = 0x02; b[4] = 0xB2; b[5] = 0x29;
    b[6] = (BYTE)n; b[7] = 0;
    b[8] = (BYTE)k; b[9] = 0;
    return hfs_put(f, "NOTEPAD29b20284", b, k);
}

int main(int argc, char **argv)
{
    HFS *f = calloc(1, sizeof *f);
    FILE *o;
    if (argc != 3)
        return 2;
    if (!strncmp(argv[1], "ann", 3)) {
        BYTE link0[2] = { 0, 0 };
        hfs_new(f, 128);
        hfs_put(f, "@LINK", link0, 2);
        hfs_put(f, "@VERSION", version, 6);
        if (argv[1][3] == '1') {
            BYTE link1[14] = { 1, 0, 0x4E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
            hfs_put(f, "78!0", (const BYTE *)"A note.", 7);
            hfs_put(f, "@LINK", link1, 14);
        }
    } else {
        hfs_new(f, 1024);
        bookmarks(f, 1);
        if (argv[1][3] == '2')
            bookmarks(f, 2);
    }
    o = fopen(argv[2], "wb");
    if (!o)
        return 1;
    fwrite(f->buf, 1, f->size, o);
    fclose(o);
    return 0;
}
