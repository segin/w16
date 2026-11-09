/*
 * paste.c - w16 Calculator: Edit > Paste, and the keyboard's translation
 * in the standard view.
 *
 * Calculator's Help (CALC.HLP, "Using Calculator with the Clipboard")
 * documents the codes :c :e :m :p :q :r and \. Observed (CA1 paste
 * probes, ARCHITECTURE.md 15.1):
 *  - each character goes through the keyboard's table (accel.h), letters
 *    in either case; blanks, line ends and unknown characters are skipped;
 *  - ':' and a digit n is the function key Fn (":9" is +/-, ":5" Hex);
 *  - ":c" MC, ":m" MS, ":p" M+, ":r" MR; ":e" Exp in the decimal
 *    scientific view; ":q" did nothing; '\' is Dat;
 *  - 'E' is Exp in decimal (and the digit E in hex);
 *  - the whole clipboard block is read, NUL bytes skipped (so bytes after
 *    the text's end count too; GMEM_ZEROINIT clipboard data avoids that);
 *  - the standard view ignores what it has no button for (sin, n!, x^y,
 *    Hex ...); there % is the percent button and @ the square root.
 */
#include "calc.h"

/* a character as Calculator's keyboard maps it (accel.h) */
static int char_command(int c)
{
    if (c >= 'a' && c <= 'z')
        c -= 32;
    switch (c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        return c;
    case 'A': case 'B': case 'C': case 'D': case 'F':
        return c;
    case 'E':
        return g.radix == 10 ? IDC_EXP : c;
    case '!': return IDC_FACT;
    case 'S': return IDC_SIN;
    case 'O': return IDC_COS;
    case 'T': return IDC_TAN;
    case 'R': return IDC_RECIP;
    case 'Y': return IDC_PWR;
    case '#': return IDC_CUBE;
    case '@': return IDC_SQR;
    case 'M': return IDC_DMS;
    case 'N': return IDC_LN;
    case 'L': return IDC_LOG;
    case 'V': return IDC_FE;
    case 'X': return IDC_EXP;
    case 'I': return IDC_INV;
    case 'H': return IDC_HYP;
    case 'P': return IDC_PI;
    case '/': return IDC_DIV;
    case '*': return IDC_MUL;
    case '%': return IDC_MOD;
    case '-': return IDC_SUB;
    case '=': return IDC_EQU;
    case '+': return IDC_ADD;
    case '&': return IDC_AND;
    case '|': return IDC_OR;
    case '~': return IDC_NOT;
    case '^': return IDC_XOR;
    case '(': return IDC_LPAREN;
    case ')': return IDC_RPAREN;
    case ';': return IDC_INT;
    case '<': return IDC_LSH;
    case '.': case ',': return IDC_POINT;
    case '\\': return IDC_DAT;
    }
    return 0;
}

/* the standard view's meaning of a key's command; 0 when the standard
   view has no such button */
int calc_std_key(int id)
{
    if ((id >= IDC_0 && id <= IDC_0 + 9))
        return id;
    switch (id) {
    case IDC_MOD: return IDC_PERCENT;
    case IDC_SQR: return IDC_SQRT;
    case IDC_POINT: case IDC_ADD: case IDC_SUB: case IDC_MUL: case IDC_DIV:
    case IDC_EQU: case IDC_RECIP: case IDC_SIGN: case IDC_CLEAR: case IDC_CE:
    case IDC_BACK: case IDC_MC: case IDC_MR: case IDC_MS: case IDC_MPLUS:
    case IDC_SIZE1: case IDC_SIZE2: case IDC_SIZE3: case IDC_DAT:
    case IDM_COPY: case IDM_PASTE: case IDM_ABOUT: case IDM_HELPCONTENTS:
        return id;
    }
    return 0;
}

static const int fkeys[9] = {
    IDM_HELPCONTENTS, IDC_SIZE1, IDC_SIZE2, IDC_SIZE3, IDC_HEX, IDC_DEC, IDC_OCT, IDC_BIN, IDC_SIGN
};

void calc_paste(const char far *s, unsigned n, void (*cmd)(int, int))
{
    unsigned i;
    for (i = 0; i < n; i++) {
        int c = (unsigned char)s[i], id = 0;
        if (c == ':' && i + 1 < n) {
            int k = (unsigned char)s[++i];
            if (k >= '1' && k <= '9') {
                id = fkeys[k - '1'];
                if (id == IDM_HELPCONTENTS)
                    id = 0;
            } else {
                if (k >= 'A' && k <= 'Z')
                    k += 32;
                switch (k) {
                case 'c': id = IDC_MC; break;
                case 'e': id = g.radix == 10 && g.sci ? IDC_EXP : 0; break;
                case 'm': id = IDC_MS; break;
                case 'p': id = IDC_MPLUS; break;
                case 'r': id = IDC_MR; break;
                }
            }
        } else {
            id = char_command(c);
        }
        if (id && !g.sci)
            id = calc_std_key(id);
        if (id)
            cmd(id, 0);
    }
}
