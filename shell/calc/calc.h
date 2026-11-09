/*
 * calc.h - w16 Calculator: shared declarations.
 *
 * The calculator is split into a portable engine (engine.c, fmt.c, the
 * floating-point helpers in fpu.c) and the Windows user interface
 * (main.c, draw.c, stat.c, clip.c, help.c). The engine is also built for
 * the host (tools/calchost.c) so that its results can be compared with
 * the original Calculator's quickly; see tests/calcscenarios.sh.
 *
 * Every behaviour here was observed on Windows 3.1's CALC.EXE with
 * scripted input (tests/apps/ca1.c, CA1); ARCHITECTURE.md 15.3 has the
 * findings. Button IDs are Calculator's own (its accelerator table, read
 * as data).
 */
#ifndef CALC_H
#define CALC_H

/* ---- command IDs (WM_COMMAND) ---- */
#define IDC_LPAREN      40
#define IDC_RPAREN      41
#define IDC_0           48          /* '0'..'9' = 48..57, 'A'..'F' = 65..70 */
#define IDC_A           65
#define IDC_SIGN        80          /* +/- */
#define IDC_CLEAR       81          /* C */
#define IDC_CE          82
#define IDC_BACK        83
#define IDC_STA         84
#define IDC_POINT       85
#define IDC_AND         86
#define IDC_OR          87
#define IDC_XOR         88
#define IDC_LSH         89
#define IDC_DIV         90
#define IDC_MUL         91
#define IDC_ADD         92
#define IDC_SUB         93
#define IDC_MOD         94          /* standard view: % */
#define IDC_PWR         95          /* x^y */
#define IDC_INT         96
#define IDC_NOT         97
#define IDC_SIN         98
#define IDC_COS         99
#define IDC_TAN         100
#define IDC_LN          101
#define IDC_LOG         102
#define IDC_SQR         103         /* x^2; standard view: sqrt */
#define IDC_CUBE        104
#define IDC_FACT        105
#define IDC_RECIP       106
#define IDC_DMS         107
#define IDC_PERCENT     108         /* standard view's % button (internal) */
#define IDC_FE          109
#define IDC_PI          110
#define IDC_EQU         111
#define IDC_MC          112
#define IDC_MR          113
#define IDC_MS          114
#define IDC_MPLUS       115
#define IDC_EXP         116
#define IDC_AVE         117
#define IDC_SUM         118
#define IDC_DEV         119         /* s */
#define IDC_DAT         120
#define IDC_BIN         121
#define IDC_OCT         122
#define IDC_DEC         123
#define IDC_HEX         124
#define IDC_INV         125
#define IDC_HYP         126
#define IDC_SIZE1       127         /* Dword / Deg */
#define IDC_SIZE2       128         /* Word / Rad */
#define IDC_SIZE3       129         /* Byte / Grad */
#define IDC_SQRT        130         /* standard view's sqrt button (internal) */

#define IDM_COPY        300
#define IDM_PASTE       301
#define IDM_ABOUT       303
#define IDM_SCIENTIFIC  305
#define IDM_STANDARD    306
#define IDM_HELPSEARCH  33
#define IDM_HELPCONTENTS 0xFFFF
#define IDM_HELPHELP    0xFFFC

/* dialog controls (the original's IDs) */
#define IDD_STATTEXT    400         /* "stat" */
#define IDD_MEMSCI      401         /* " M" in the scientific view */
#define IDD_MEMSTD      402         /* " M" in the standard view */
#define IDD_PARENS      403         /* "(=n" */
#define IDD_DISPSCI     413
#define IDD_DISPSTD     414
#define IDD_EDIT        495         /* hidden edit control for Copy */
#define IDD_STATLIST    407
#define IDD_STATN       408
#define IDD_STATNL      409
#define IDD_LOAD        410
#define IDD_RET         411
#define IDD_CD          404
#define IDD_CAD         405

/* strings */
#define IDS_FIRSTKEY    0           /* 0..63: button labels */
#define IDS_DEG         64
#define IDS_ERRDIV0     67
#define IDS_ERRINVALID  68
#define IDS_ERRUNDEF    69
#define IDS_ERRBIG      70
#define IDS_ERRSMALL    71
#define IDS_HELPFILE    72
#define IDS_CLIPBUSY    73
#define IDS_NOMEM       74
#define IDS_STAT        75
#define IDS_CALC        76
#define IDS_ABOUT       77

/* errors (the string to show) */
#define ERR_NONE        0
#define ERR_DIV0        IDS_ERRDIV0
#define ERR_INVALID     IDS_ERRINVALID
#define ERR_UNDEF       IDS_ERRUNDEF
#define ERR_BIG         IDS_ERRBIG
#define ERR_SMALL       IDS_ERRSMALL
#define ERR_SILENT      1           /* error state with the display unchanged */

/* 32-bit unsigned (the bitwise operators, hex/octal/binary) */
#ifdef __WATCOMC__
typedef unsigned long U32;
#else
typedef unsigned int U32;
#endif

/* ---- engine state ---- */
#define MAXPAREN        25
#define MAXOPS          8           /* pending operators per parenthesis level */

typedef struct {
    double v[MAXOPS];
    int op[MAXOPS];
    int n;
} FRAME;

typedef struct {
    int sci;                        /* scientific view */
    int radix;                      /* 10, 16, 8, 2 */
    int wordsize;                   /* 0 dword, 1 word, 2 byte */
    int angle;                      /* 0 deg, 1 rad, 2 grad */
    int inv, hyp;
    int fe;                         /* F-E: scientific notation */
    int err;                        /* ERR_* */

    double x;                       /* the current number */
    int newnum;                     /* x was entered or computed since the last operator */
    int entering;                   /* digits are being typed into x */
    int neg;                        /* entry: minus sign */
    char idig[400];                 /* entry: integer digits (decimal) */
    int nidig;
    char fdig[400];                 /* entry: fraction digits */
    int nfdig;
    int point;                      /* entry: decimal point typed (kept after the entry) */
    int lastzero;                   /* the last key typed was the digit 0 */
    int prevzero;                   /* lastzero before the current key */
    int expmode, expneg, expdigits; /* entry: exponent */
    int expval;
    double mant;                    /* entry: mantissa value */
    int mantzero;                   /* entry: the mantissa's last digit was 0 */
    int xfixed;                     /* entry: x kept as it is (from a result in e-notation) */
    int ceafter;                    /* CE since the last = */
    int parens;                     /* '(' pressed in a row */
    int showparen;                  /* the display shows that many '(' */

    FRAME fr[MAXPAREN + 1];
    int depth;                      /* open parentheses */
    int opjust;                     /* the last key was a binary operator */
    int lastop;                     /* for repeated = */
    double lastopnd;
    int havelast;

    double mem;
    int stat;                       /* statistics box open */

    char disp[64];                  /* display text */
} CALC;

extern CALC g;

/* engine.c */
void calc_init(void);
void calc_clear(void);
void calc_load(double v);
double calc_pow(double a, double b);
double int_result(double v);
void calc_command(int id);
void calc_set_view(int sci);
void calc_display(void);            /* redisplay x (as after Copy) */
int calc_copytext(char *buf, int n);

/* hooks the user interface provides */
int ui_toowide(const char *s);      /* the text is wider than the standard display */
void ui_stat_add(double v, const char *text);  /* Dat */
int ui_stat_count(void);
double ui_stat_get(int i);
void ui_beep(void);

#ifndef __WATCOMC__
#define far
#endif

/* paste.c: the clipboard text as keys, cmd(id, 0) for each; the
   standard view's meaning of a key's command (0: none) */
void calc_paste(const char far *s, unsigned n, void (*cmd)(int, int));
int calc_std_key(int id);

/* the standard display's text width limit (pixels of the system font):
   observed, 176 fits and 184 does not; UNVERIFIED in between */
#define CALC_STD_TEXTMAX 178

/* fmt.c */
void fmt_number(double x, char *out, int sci, int radix, int wordsize);
double parse_entry(void);

/* fpu.c: floating-point helpers (x87, extended precision inside) */
typedef struct { unsigned char b[10]; } EXT;
void fp_init(void);
double fp_frac_digit(double x, int d, EXT *sc);
void fp_frac_start(EXT *sc);
int fp_ilog10(double a);
int fp_digits(double a, int n, char *out);
double fp_trunc(double x);
double fp_floor(double x);
double fp_sqrt(double x);
double fp_ln(double x);
double fp_log10(double x);
double fp_exp(double x);
double fp_exp10(double x);
double fp_pow(double x, double y);
double fp_sin(double x);
double fp_cos(double x);
double fp_tan(double x);
double fp_atan2(double y, double x);
double fp_pi(void);
double fp_pow10i(double m, int e);
int fp_isinf(double x);
double fp_fmod(double x, double y);
U32 fp_lowbits(double x);

#endif
