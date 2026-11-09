/*
 * cdlgs.h - w16 COMMDLG: dialog template names, string IDs and control IDs.
 *
 * The template numbers and control IDs are the original's (read from its
 * dialog resources as data, tools/nedlg.py; applications that bring their
 * own templates use them, and the Windows 3.1 SDK's dlgs.h names most of
 * them: stc1 = 0x440, lst1 = 0x460, cmb1 = 0x470, edt1 = 0x480, psh1 =
 * 0x400, chx1 = 0x410, rad1 = 0x420, grp1 = 0x430, pshHelp = 0x40E). The
 * colour dialog's IDs have no names in dlgs.h; ours are below.
 * Included by commdlg.rc too: preprocessor definitions only.
 */
#ifndef W16_CDLGS_H
#define W16_CDLGS_H

/* templates (dlgs.h numbers; the colour dialog's is named) */
#define DLG_OPEN        1536            /* FILEOPENORD */
#define DLG_MULTIOPEN   1537            /* MULTIFILEOPENORD */
#define DLG_PRINT       1538            /* PRINTDLGORD */
#define DLG_PRNSETUP    1539            /* PRNSETUPDLGORD */
#define DLG_FIND        1540            /* FINDDLGORD */
#define DLG_REPLACE     1541            /* REPLACEDLGORD */
#define DLG_FONT        1543            /* FORMATDLGORD31 */
#define DLG_COLOR       "CHOOSECOLOR"

/* the picture strip of the file dialogs and the font dialog's pictures */
#define BMP_DRIVES      576
#define BMP_CROSS       584
#define BMP_FONTTYPE    38
#define BMP_ARROW       30
#define ICO_PORTRAIT    528
#define ICO_LANDSCAPE   529

/* strings (the original's numbers) */
#define IDS_FILEEXISTS_T    256
#define IDS_FILEEXISTS      257
#define IDS_OPEN            384
#define IDS_SAVEAS          385
#define IDS_SAVETYPE        386
#define IDS_NODRIVE         387
#define IDS_CANTREAD        388
#define IDS_DIFFDISK        389
#define IDS_UNFORMATTED     390
#define IDS_FILENOTFOUND    391
#define IDS_PATHNOTFOUND    392
#define IDS_INVALIDNAME     393
#define IDS_SHARING         394
#define IDS_ACCESSDENIED    395
#define IDS_READONLY        396
#define IDS_CRITICAL        397
#define IDS_DEVICENAME      398
#define IDS_WRITEPROTECT    399
#define IDS_DISKFULL        400
#define IDS_TOOMANYFILES    401
#define IDS_CREATEPROMPT    402
#define IDS_NETCREATEONLY   403
#define IDS_CANTSELDRIVE    404
#define IDS_COLORNAMES      1040        /* 16: Black .. White */
#define IDS_QUALITY         1072        /* High, Medium, Low, Draft */
#define IDS_PRINTER         1088
#define IDS_DEFPRINTER      1089
#define IDS_PRNONPORT       1090
#define IDS_EXTDEVICEMODE   1091
#define IDS_DEVICEMODE      1092
#define IDS_DEVICECAPS      1093
#define IDS_CURRENTLY       1094
#define IDS_PRNONPORT2      1095
#define IDS_PRINTSETUP      1096
#define IDS_SIZENUMBER      1098
#define IDS_SIZERANGE       1099
#define IDS_FONTSIMULATED   1100
#define IDS_FONTTT          1101
#define IDS_FONTPRINTER     1102
#define IDS_FONTSCREEN      1103
#define IDS_FROMBELOW       1104
#define IDS_FROMABOVE       1105
#define IDS_TOBELOW         1106
#define IDS_TOABOVE         1107
#define IDS_FROMBAD         1108
#define IDS_TOBAD           1109
#define IDS_PAGESEMPTY      1110
#define IDS_COPIESEMPTY     1111
#define IDS_COPIESBAD       1112
#define IDS_COPIESZERO      1113
#define IDS_NODEFPRINTER    1114
#define IDS_NODRIVER        1115
#define IDS_PAPERNAMES      1152        /* + DMPAPER_* (1..41) */
#define IDS_FONTS_T         1280
#define IDS_NOFONTS         1281
#define IDS_BADFACE         1282
#define IDS_BADSTYLE        1283
#define IDS_REGULAR         1284
#define IDS_BOLD            1285
#define IDS_ITALIC          1286
#define IDS_BOLDITALIC      1287
#define IDS_USERSIZE        1408

/* the colour dialog's controls (the original's IDs) */
#define IDC_CUSTDEFINE      0x2CF       /* Define Custom Colors... */
#define IDC_BASIC           0x2D0       /* the basic colours */
#define IDC_CUSTOM          0x2D1       /* the custom colours */
#define IDC_SPECTRUM        0x2C6       /* hue/saturation field */
#define IDC_LUMBAR          0x2BE       /* luminosity bar */
#define IDC_SAMPLE          0x2C5       /* Color|Solid */
#define IDC_SOLIDBTN        0x2C9       /* the hidden "&o" button */
#define IDC_HUE             0x2BF
#define IDC_SAT             0x2C0
#define IDC_LUM             0x2C1
#define IDC_RED             0x2C2
#define IDC_GREEN           0x2C3
#define IDC_BLUE            0x2C4
#define IDC_ADDCUSTOM       0x2C8
#define IDC_HUELBL          0x2D3
#define IDC_SATLBL          0x2D4
#define IDC_LUMLBL          0x2D5
#define IDC_REDLBL          0x2D6
#define IDC_GREENLBL        0x2D7
#define IDC_BLUELBL         0x2D8

#endif
