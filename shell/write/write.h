/*
 * write.h - w16 Write.
 *
 * A clean-room Write, made to match Windows 3.1's in look, behaviour and
 * the files it writes. Every detail comes from black-box observation of
 * the original on the reference install (probes WR*, tests/wrobs.sh;
 * resource templates read as data with tools/nerc.py) and is written
 * down in docs/wri-format.md (the file format) and ARCHITECTURE.md
 * ("Write" under 15 Applets).
 *
 * Built medium model, no C runtime; Windows 3.1 only.
 */
#ifndef WRITE_H
#define WRITE_H

#include <windows.h>

/* ---- command IDs: the original's (its MW_MENU resource, tools/nerc.py),
   so that SNAP's "cmd" works on both ---- */
#define IDM_NEW             4097
#define IDM_OPEN            4098
#define IDM_SAVE            4099
#define IDM_SAVEAS          4100
#define IDM_PRINT           4101
#define IDM_PRINTSETUP      4102
#define IDM_REPAGINATE      4103
#define IDM_EXIT            4104
#define IDM_UNDO            4112
#define IDM_CUT             4113
#define IDM_COPY            4114
#define IDM_PASTE           4115
#define IDM_MOVEPICT        4116
#define IDM_SIZEPICT        4117
#define IDM_PASTELINK       4118
#define IDM_LINKS           4119
#define IDM_INSERTOBJ       4120
#define IDM_PASTESPECIAL    4121
#define IDM_OBJECT          5120
#define IDM_FIND            4128
#define IDM_FINDAGAIN       4129
#define IDM_REPLACE         4130
#define IDM_GOTO            4131
#define IDM_REGULAR         4160
#define IDM_BOLD            4161
#define IDM_ITALIC          4162
#define IDM_UNDERLINE       4163
#define IDM_SUPER           4164
#define IDM_SUB             4165
#define IDM_REDUCE          4166
#define IDM_ENLARGE         4167
#define IDM_FONTS           4168
#define IDM_NORMAL          4224
#define IDM_LEFT            4225
#define IDM_CENTER          4226
#define IDM_RIGHT           4227
#define IDM_JUSTIFY         4228
#define IDM_SINGLE          4229
#define IDM_ONEHALF         4230
#define IDM_DOUBLE          4231
#define IDM_INDENTS         4232
#define IDM_HEADER          4352
#define IDM_FOOTER          4353
#define IDM_RULER           4354
#define IDM_TABS            4355
#define IDM_PAGELAYOUT      4356
#define IDM_HELPCONTENTS    4608
#define IDM_HELPSEARCH      4609
#define IDM_HELPHELP        4610
#define IDM_ABOUT           4611
#define IDM_HELPF1          62192       /* F1's accelerator: Help Index (WR11) */

/* dialogs: the original's resource IDs */
#define IDD_CONVERT         3
#define IDD_PRINTING        6
#define IDD_REPAGINATE      7
#define IDD_REPAGINATING    8
#define IDD_CONFIRMBREAK    9
#define IDD_KEEPBREAK       10
#define IDD_FIND            12
#define IDD_REPLACE         13
#define IDD_GOTO            14
#define IDD_INDENTS         16
#define IDD_HEADER          17
#define IDD_FOOTER          18
#define IDD_TABS            19
#define IDD_PAGELAYOUT      20
#define IDD_MARGINS         21

/* strings: the original's IDs (string tables, tools/nerc.py) */
#define IDS_UNDO            1
#define IDS_EDITING         2
#define IDS_FORMATTING      3
#define IDS_TYPING          4
#define IDS_RULERON         5
#define IDS_RULEROFF        6
#define IDS_HELPFILE        11
#define IDS_PAGE            14
#define IDS_LOADING         15
#define IDS_REPLACEEXIST    22
#define IDS_TEXTDOC         26
#define IDS_WORDDOC         27
#define IDS_OPENTITLE       96
#define IDS_SAVEASTITLE     97
#define IDS_EXTWRI          98
#define IDS_EXTDOC          99
#define IDS_EXTTXT          100
#define IDS_FLTTXT          101
#define IDS_FLTWRI          102
#define IDS_FLTDOC          103
#define IDS_FLTDOCTXT       104
#define IDS_FLTWRI30        105
#define IDS_FLTALL          106
#define IDS_BACKUP          111
#define IDS_SEARCHDONE      4096
#define IDS_NOTFOUND        4097
#define IDS_NOREPLACE       4098
#define IDS_CONVERTQ        12290
#define IDS_READONLY        8224
#define IDS_CORRUPT         8235
#define IDS_BADNAME         8236
#define IDS_CANTREAD        8237
#define IDS_DAMAGED         8198
#define IDS_NOPAGE          8199
#define IDS_NOTNUMBER       8200
#define IDS_FONTSIZE        8201
#define IDS_WHOLENUMBER     8202
#define IDS_BADMEASURE      8203
#define IDS_MEASUREPOS      8204
#define IDS_MARGINSBIG      8205
#define IDS_NOTFOUNDFILE    8217
#define IDS_SAVECHANGES     8218
#define IDS_NOMEMORY        8219
#define IDS_NOPATH          8220
#define IDS_CANTSAVE        16390
#define IDS_SAVE30          8233
#define IDS_NOPICTURES      8234
#define IDS_CANTPRINT       8226
#define IDS_OBJECTNONE      32
#define IDS_AUTOMATIC       34
#define IDS_FMTBITMAP       107
#define IDS_FMTPICTURE      108
#define IDS_FMTDIB          109
#define IDS_FMTTEXT         110
#define IDS_FMTOBJECT       112
#define IDS_MANUAL          35
#define IDS_UNAVAILABLE     36
#define IDS_NOSOURCE        8470
#define IDS_UPDATELINKS     8473
#define IDS_UNDOKEY         28692
#define IDS_PAGE1           28672
#define IDS_WRITE           28676
#define IDS_UNTITLED        28677
#define IDS_HEADERNAME      28682
#define IDS_FOOTERNAME      28683
#define IDS_INCH            28686
#define IDS_CM              28687

#define IDA_MAIN            1           /* accelerators */
#define IDI_WRITE           1

/* ---- huge memory: growable global blocks addressed by 32-bit offsets
   (util.c). ZOI: sizes are bounded only by DWORD and available memory;
   the .WRI format itself bounds a document at 65535 pages of 128 bytes
   (16-bit page numbers), about 8 MB. ---- */
typedef struct {
    HGLOBAL h;
    BYTE FAR *p;                        /* locked base, offset 0 */
    DWORD size;
} HMEM;

BOOL hm_resize(HMEM *m, DWORD size);    /* grow or shrink, keeping data */
void hm_free(HMEM *m);
BYTE FAR *hm_ptr(const HMEM *m, DWORD off);
/* bytes from off that are contiguous in memory (to the 64K boundary) */
WORD hm_span(DWORD off, DWORD want);
void hm_move(HMEM *m, DWORD dst, DWORD src, DWORD n);
void hm_get(const HMEM *m, DWORD off, void FAR *dst, DWORD n);
void hm_put(HMEM *m, DWORD off, const void FAR *src, DWORD n);
WORD ahincr(void);                      /* i4.asm: KERNEL's __AHINCR */

/* ---- strings and numbers (util.c) ---- */
int  s_len(const char FAR *s);
void s_cpy(char FAR *d, const char FAR *s, int max);
void s_cat(char FAR *d, const char FAR *s, int max);
int  s_icmp(const char FAR *a, const char FAR *b);
void s_utoa(char FAR *d, DWORD v);
void m_set(void FAR *d, BYTE v, WORD n);
void m_cpy(void FAR *d, const void FAR *s, WORD n);
int  m_cmp(const void FAR *a, const void FAR *b, WORD n);
int  msgbox(HWND owner, WORD ids, const char FAR *arg, UINT flags);
void load_str(WORD ids, char *buf, int size);

/* ---- the document (doc.c) ---- */

/* character properties: the bytes of a CHP as Write stores them
   (docs/wri-format.md); default {1, 0, 24, 0, 0, 0} */
#define CHP_MAX     255                 /* the format: FPROP cch is a byte */
/* paragraph properties: PAP bytes; 22 fixed bytes, then 14 tab stops of
   4 bytes (format bound: 14 tabs, PAP 78 bytes) */
#define PAP_TABS    22
#define NTABS       14
#define PAP_LEN     (PAP_TABS + 4 * NTABS)

typedef DWORD ATOM32;                   /* an interned property byte string */

typedef struct {                        /* decoded CHP */
    WORD ftc;                           /* font code: font table index */
    BYTE hps;                           /* size, half points */
    BYTE bold, italic, underline;
    signed char hpsPos;                 /* > 0 superscript, < 0 subscript */
    BYTE b0;                            /* byte 0 (1 = "Regular", docs) */
    BYTE special;                       /* byte 3 bit 6: "(page)" (SDK ch. 8) */
} CHPX;

typedef struct {                        /* decoded PAP */
    BYTE b0;                            /* byte 0: 61 default, 60 formatted */
    BYTE jc;                            /* 0 left 1 center 2 right 3 justify */
    int dxaRight, dxaLeft, dxaLeft1;    /* twips */
    int dyaLine;                        /* 240 single, 360, 480 */
    BYTE rhc;                           /* running head bits */
    BYTE fGraphics;                     /* picture paragraph */
    int ntabs;
    struct { int dxa; BYTE jc; } tab[NTABS];
} PAPX;

typedef struct {                        /* section (page layout) */
    WORD yaMac, xaMac;                  /* page size, twips */
    WORD pgnFirst;                      /* first page number */
    WORD pgnRaw;                        /* as stored: FFFFh for the default */
    WORD yaTop, dyaText, xaLeft, dxaText;
    WORD yaHeader, yaFooter;            /* header/footer distance */
} SEPX;

typedef struct {
    char name[LF_FACESIZE + 1];
    BYTE ffid;                          /* family (lfPitchAndFamily) */
} FFN;

typedef struct {
    /* text: a gap buffer (bytes, cp = character position) */
    HMEM text;
    DWORD cpMac;                        /* characters in the document */
    DWORD gap, gapLen;
    /* character runs: {cpLim, atom}, the last cpLim = cpMac */
    HMEM runs;
    DWORD nruns;
    /* paragraphs: {cpLim, atom}; the last one is the end paragraph,
       cpLim = cpMac + 2 (Write's implicit end mark) */
    HMEM paras;
    DWORD nparas;
    /* interned property strings: offsets into pool */
    HMEM atoms, pool;
    DWORD natoms, poolLen;
    /* fonts */
    HMEM fonts;                         /* FFN array */
    DWORD nfonts;
    BOOL hasFfntb;                      /* the file had a font table */
    /* section */
    BYTE sep[CHP_MAX + 1];              /* raw SEP bytes, sep[0] = cch */
    BOOL hasSep;
    WORD sedFn;                         /* SETB's second word, kept */
    /* page table */
    HMEM pgtb;                          /* {WORD pgn; DWORD cp} as 8 bytes */
    DWORD npages;
    WORD pgtbMax;                       /* PGTB's second word: its room */
    ATOM32 chpNew;                      /* properties when there is no text */
    WORD ident;                         /* BE31 or BE32 */
    BOOL dirty;
} DOC;

extern DOC g_doc;

void   doc_init(DOC *d);
void   doc_new(DOC *d);
void   doc_free(DOC *d);
BOOL   doc_copy(DOC *d, const DOC *s);
BOOL   doc_has_pictures(DOC *d);
void   doc_strip_pictures(DOC *d);
BYTE   doc_ch(DOC *d, DWORD cp);
DWORD  doc_get(DOC *d, DWORD cp, char FAR *buf, DWORD n);
ATOM32 atom_intern(DOC *d, const BYTE FAR *b, WORD len);
WORD   atom_get(DOC *d, ATOM32 a, BYTE FAR *b);     /* returns len */
ATOM32 chp_atom(DOC *d, const CHPX *c);
void   chp_decode(DOC *d, ATOM32 a, CHPX *c);
ATOM32 pap_atom(DOC *d, const PAPX *p);
void   pap_decode(DOC *d, ATOM32 a, PAPX *p);
void   chp_default(CHPX *c);
void   pap_default(PAPX *p);
void   sep_decode(DOC *d, SEPX *s);
void   sep_encode(DOC *d, const SEPX *s);
/* runs and paragraphs */
DWORD  run_at(DOC *d, DWORD cp, DWORD *cpFirst, DWORD *cpLim, ATOM32 *a);
DWORD  para_at(DOC *d, DWORD cp, DWORD *cpFirst, DWORD *cpLim, ATOM32 *a);
void   para_get(DOC *d, DWORD i, DWORD *cpFirst, DWORD *cpLim, ATOM32 *a);
void   para_set(DOC *d, DWORD i, ATOM32 a);
void   run_get(DOC *d, DWORD i, DWORD *cpLim, ATOM32 *a);
/* editing */
BOOL   doc_insert(DOC *d, DWORD cp, const char FAR *s, DWORD n, ATOM32 chp);
BOOL   doc_delete(DOC *d, DWORD cp, DWORD n);
void   doc_put(DOC *d, DWORD cp, const void FAR *s, DWORD n);   /* overwrite bytes */
BOOL   doc_set_chp(DOC *d, DWORD cp, DWORD cpLim, ATOM32 (*fn)(DOC *, ATOM32, WORD), WORD arg);
BOOL   doc_terminate_last(DOC *d, ATOM32 chp);
BOOL   doc_insert_para(DOC *d, DWORD cp, const char FAR *s, DWORD n, ATOM32 chp, ATOM32 pap);
BOOL   doc_append_para(DOC *d, DWORD cp, const char FAR *s, DWORD n, ATOM32 chp);
WORD   font_code(DOC *d, const char FAR *name, BYTE ffid);
FFN FAR *font_get(DOC *d, WORD ftc);
/* files (wrifile.c) */
int    doc_load(DOC *d, LPCSTR path, BOOL *isText);
int    doc_load_text(DOC *d, LPCSTR path);
int    doc_save(DOC *d, LPCSTR path, int format);
#define FMT_WRI     0
#define FMT_WORD    1
#define FMT_TEXT    2
#define FMT_WORDTXT 3                   /* Word for DOS/Txt Only: text (WR23) */
#define FMT_WRI30   4                   /* 3.0 Write: without objects (WR35) */
#define LOAD_OK         0
#define LOAD_NOFILE     1
#define LOAD_NOTWRI     2               /* text or Word file: ask to convert */
#define LOAD_CORRUPT    3
#define LOAD_NOMEM      4

/* ---- pictures (pict.c) ---- */
#define PK_NONE     0                   /* unknown: drawn as a frame */
#define PK_MF       1                   /* metafile */
#define PK_DIB      2                   /* device-independent bitmap */
#define PK_BITMAP   3                   /* Write 3.0 bitmap picture */
#define PK_OBITMAP  4                   /* an object's BITMAP presentation */
typedef struct {
    WORD mm;
    int xExt, yExt, mfMM;
    int dxaOffset, dxaSize, dyaSize;
    WORD mx, my;
    int kind;
    DWORD cpData, cbData;
    BITMAP bm;
} PICT;
BOOL pict_read(DWORD cp, DWORD cpLim, PICT *p);
/* OLE objects (ole.c) */
void ole_init(void);
void ole_done(void);
void ole_saved(void);
void ole_notify(WPARAM wp, LPARAM lp);
BOOL ole_draw(HDC dc, DWORD pf, DWORD pl, const RECT *r);
void ole_activate(DWORD pf, DWORD pl, const RECT *r);
void cmd_insertobj(void);
BOOL ole_paste(void);
BOOL ole_can_pastelink(void);
void ole_pastelink(void);
void cmd_links(void);
void cmd_pastespecial(void);
void ole_menu(HMENU m);
BOOL ole_verb(WORD id);
void ole_opened(int show);   /* asks to update links (WR33) */
extern BOOL g_holdPaint;       /* view.c: text not drawn meanwhile */
void pict_draw(HDC dc, const PICT *p, int x, int y, int w, int h);

/* ---- the view (view.c) ---- */
extern HINSTANCE g_hinst;
extern HWND g_frame, g_docwnd, g_pageinfo, g_hscroll, g_vscroll;
extern DWORD g_selFirst, g_selLim;      /* selection; caret when equal */
extern ATOM32 g_insChp;                 /* properties for typing */
extern BOOL g_insChpValid;
BOOL  view_register(HINSTANCE);
void  view_reset(void);
void  view_caret_now(void);
void  view_caret_menu(void);
void  view_caret_defer(void);
BOOL  view_last_typed(void);
void  view_changed(DWORD cp);           /* text changed at or after cp */
void  view_select(DWORD first, DWORD lim, BOOL show);
void  view_layout(void);
void  view_update_pageinfo(void);
void  view_scroll_to_caret(void);
DWORD view_line_before(DWORD cp);       /* the line starts around cp's line */
DWORD view_line_after(DWORD cp);
DWORD view_line_end(DWORD cp);
void  view_show_line(DWORD cp);          /* its text's end, no paragraph mark */
LRESULT FAR PASCAL __export DocWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT FAR PASCAL __export PageInfoWndProc(HWND, UINT, WPARAM, LPARAM);
void  view_key(WPARAM vk);
void  view_char(WORD ch);
void  sel_props(CHPX *c);
BOOL  view_pict_selected(void);
BOOL  view_pict_mode(void);
void  view_pict_start(int mode);
BOOL  view_pict_scale(char *s);

/* ---- commands (cmds.c), main (main.c) ---- */
void  cmd_char(WORD id);
void  cmd_undo(void);
int   hf_mode(void);
HWND  hf_dialog(void);
void  hf_tabs(const PAPX *t);          /* tabs set in the head's view, for the body */
BOOL  parse_measure(const char *s, long *tw, BOOL allowNeg);
void  fmt_measure(char *s, long tw);
void  cmd_print(void);
void  cmd_printsetup(void);
void  printer_font(char *face, BYTE *ffid);
HDC   printer_ic(void);
void  cmd_repaginate(void);
void  undo_save(WORD kind);
void  undo_end_typing(void);
BOOL  undo_typing(void);
void  undo_clear(void);
void  cmd_para(WORD id);
#define PA_ALL       1                  /* every paragraph, not the selected */
#define PA_NORMAL    2                  /* Paragraph > Normal */
#define PA_TERMINATE 4                  /* with PA_ALL: end the last paragraph */
#define PA_KEEPB0    8                  /* PAP byte 0 as it was */
void  cmd_para_how(WORD id, WORD how);
void  pap_apply(void (*fn)(PAPX *, WORD), WORD arg, WORD how);
/* the ruler (ruler.c) */
extern HWND g_ruler;
extern BOOL g_cm;
BOOL  ruler_register(HINSTANCE);
void  ruler_toggle(void);
void  ruler_update(void);
void  ruler_done(void);
int   ruler_height(void);
void  frame_layout(int cx, int cy);
void  cmd_indents(void);
void  cmd_tabs(void);
void  cmd_pagelayout(void);
void  cmd_goto(void);
void  cmd_find(BOOL replace);
void  cmd_findagain(void);
void  cmd_fonts(void);
void  cmd_cut_copy(BOOL cut);
void  cmd_paste(void);
void  cmd_header(BOOL footer);
void  menu_update(HMENU m);
void  set_title(void);
extern char g_path[];                   /* document path, "" if Untitled */
#define PATH_MAX_DOS 144                /* DOS: drive + 64-char path + name */
extern BOOL g_backup;
HWND  find_dialog(void);
void  dos_rename(LPCSTR from, LPCSTR to);   /* i4.asm */

#endif
