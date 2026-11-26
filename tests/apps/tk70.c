/*
 * tk70.c - Microsoft's OLE 2 libraries (COMPOBJ, OLE2, STORAGE: the
 * redistributable 16-bit OLE 2.0 that programs install) starting up on
 * KERNEL: their versions, CoInitialize, OleInitialize, a temporary
 * compound file and one on C:, and the ends. StarOffice 3.1 said "Error
 * initializing object-system." on w16's KERNEL. The libraries are not
 * w16's and not in the repository: the test copies an installed set in
 * (the boot-test case staroff). Started from WIN.INI run= (Program
 * Manager the shell); writes C:\TK70.LOG, one line per call: the call and
 * its result (DX:AX), and "T done".
 */
#include <windows.h>

typedef DWORD HRES;             /* OLE 2's HRESULT (16-bit: 32 bits) */

extern DWORD FAR PASCAL COBUILDVERSION(void);                   /* COMPOBJ.1 */
extern HRES FAR PASCAL COINITIALIZE(void FAR *malloc);          /* COMPOBJ.2 */
extern void FAR PASCAL COUNINITIALIZE(void);                    /* COMPOBJ.3 */
extern HRES FAR PASCAL COGETMALLOC(DWORD ctx, void FAR *FAR *pm); /* COMPOBJ.4 */
extern DWORD FAR PASCAL OLEBUILDVERSION(void);                  /* OLE2.1 */
extern HRES FAR PASCAL OLEINITIALIZE(void FAR *malloc);         /* OLE2.2 */
extern void FAR PASCAL OLEUNINITIALIZE(void);                   /* OLE2.3 */
extern HRES FAR PASCAL STGCREATEDOCFILE(const char FAR *name, DWORD mode,
                                        DWORD reserved, void FAR *FAR *stg); /* STORAGE.1 */

extern int __cdecl dos_int21(WORD FAR *r);
extern WORD __cdecl init_regs[7];    /* InitTask's ax bx cx dx si di es */
extern BOOL FAR PASCAL INITAPP(HINSTANCE inst);                 /* USER.5 */

/* INT 2Fh with AX in, AX out */
WORD int2f(WORD ax);
#pragma aux int2f = "int 2Fh" parm [ax] value [ax];

static char line[200];
static int ln;

static void put(const char *s)
{
    while (*s)
        line[ln++] = *s++;
}

static void hex(DWORD v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--)
        line[ln++] = "0123456789ABCDEF"[(WORD)(v >> (4 * i)) & 15];
}

static void flush(void)
{
    HFILE f = _lopen("C:\\TK70.LOG", OF_READWRITE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\TK70.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    line[ln++] = '\r';
    line[ln++] = '\n';
    _lwrite(f, line, ln);
    _lclose(f);
    ln = 0;
}

static void res(const char *what, DWORD v)
{
    put(what);
    put("=");
    hex(v, 8);
    flush();
}

/* IUnknown::Release, the third entry of the object's table (16-bit OLE 2
   methods are far cdecl) */
typedef DWORD (FAR __cdecl *releasefn)(void FAR *self);

static DWORD release(void FAR *obj)
{
    releasefn FAR *vt = *(releasefn FAR *FAR *)obj;
    return vt[2](obj);
}

#define STGM_READWRITE          0x00000002L
#define STGM_SHARE_EXCLUSIVE    0x00000010L
#define STGM_CREATE             0x00001000L
#define STGM_DELETEONRELEASE    0x04000000L

int __cdecl tmain(void)
{
    void FAR *p;
    HRES h;
    OFSTRUCT of;

    /* a message queue: OLE 2 makes windows and sends messages (without
       one, OleInitialize never returned on 3.1) */
    INITAPP((HINSTANCE)init_regs[5]);
    put("T start mode=");
    put(GetWinFlags() & WF_ENHANCED ? "E" : "S");
    flush();
    /* the drives (the test mounts the CD as D:, DOSBox's MSCDEX) */
    {
        int d;
        put("GetDriveType");
        for (d = 0; d < 5; d++) {
            put(" ");
            hex(GetDriveType(d), 1);
        }
        flush();
    }
    res("CoBuildVersion", COBUILDVERSION());
    res("OleBuildVersion", OLEBUILDVERSION());
    res("CoInitialize", COINITIALIZE(NULL));
    p = NULL;
    h = COGETMALLOC(1, &p);
    res("CoGetMalloc", h);
    put("malloc=");
    put(p ? "1" : "0");
    flush();
    /* what a program can see of SHARE: INT 2Fh 1000h, and a lock of a
       file's first byte (INT 21h 5C00h) */
    res("INT 2Fh 1000h AL", int2f(0x1000) & 0xFF);
    {
        HFILE f = _lcreat("C:\\TK70.TMP", 0);
        WORD r[8];
        int i, cf;
        for (i = 0; i < 8; i++)
            r[i] = 0;
        r[0] = 0x5C00;
        r[1] = (WORD)f;
        r[5] = 1;                       /* DI: the length's low word */
        cf = dos_int21((WORD FAR *)r);
        put("INT 21h 5C00h CF=");
        hex(cf, 1);
        put(" AX=");
        hex(cf ? r[0] : 0, 4);
        flush();
        r[0] = 0x5C01;
        r[1] = (WORD)f;
        r[2] = r[3] = r[4] = 0;
        r[5] = 1;
        dos_int21((WORD FAR *)r);
        _lclose(f);
        OpenFile("C:\\TK70.TMP", &of, OF_DELETE);
    }
    p = NULL;
    h = STGCREATEDOCFILE(NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_CREATE |
                         STGM_DELETEONRELEASE, 0, &p);
    res("StgCreateDocfile temp", h);
    if (!h && p)
        res("Release", release(p));
    p = NULL;
    h = STGCREATEDOCFILE("C:\\TK70.STG", STGM_READWRITE | STGM_SHARE_EXCLUSIVE |
                         STGM_CREATE, 0, &p);
    res("StgCreateDocfile C:\\TK70.STG", h);
    if (!h && p)
        res("Release", release(p));
    put("file there=");
    put(OpenFile("C:\\TK70.STG", &of, OF_EXIST) != HFILE_ERROR ? "1" : "0");
    flush();
    OpenFile("C:\\TK70.STG", &of, OF_DELETE);
    res("OleInitialize", OLEINITIALIZE(NULL));
    OLEUNINITIALIZE();
    put("T done");
    flush();
    return 0;
}
