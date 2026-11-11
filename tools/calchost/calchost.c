/*
 * calchost - run w16 Calculator's engine (shell/calc/engine.c, fmt.c) on
 * the host with a CA1 script and print what CA1 would log
 * (tests/apps/ca1.c), so that the engine can be compared with the
 * original Calculator's logs without starting Windows.
 *
 * usage: calchost CA.TXT > ca.txt
 *
 * The user interface is simulated: the two display statics (414
 * standard, 413 scientific; the hidden one keeps its old text), the
 * indicators, Edit > Copy (copies the display, then shows x again),
 * Edit > Paste (shell/calc/paste.c), the accelerator table and the
 * standard view's buttons that have no ID of their own.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../shell/calc/calc.h"

static char disp414[64] = " 0.", disp413[64] = "", clip[256] = "";
static int m401, m402;
static void sync_display(void);
static double stats[512];
static char stattext[512][32];
static int nstats, statsel = -1;

/* system font advance widths (fonts/vgasys.txt, Windows 3.1's) */
static int charw(char c)
{
    switch (c) {
    case ' ': case '-': case '.': case '(': return 4;
    case 'B': case 'D': return 10;
    case 'C': case 'E': return 9;
    default: return 8;
    }
}

int ui_toowide(const char *s)
{
    int w = 0;
    while (*s)
        w += charw(*s++);
    return w > CALC_STD_TEXTMAX;
}

void ui_stat_add(double v, const char *t)
{
    if (nstats < 512) {
        stats[nstats] = v;
        snprintf(stattext[nstats], 32, "%s", t);
        statsel = nstats++;
    }
}

static void stat_command(int id)
{
    int i;
    if (id == 410 && statsel >= 0) {            /* LOAD */
        calc_load(stats[statsel]);
        sync_display();
    } else if (id == 404 && statsel >= 0) {     /* CD */
        for (i = statsel; i + 1 < nstats; i++) {
            stats[i] = stats[i + 1];
            memcpy(stattext[i], stattext[i + 1], 32);
        }
        nstats--;
        if (statsel >= nstats)
            statsel = nstats - 1;
    } else if (id == 405) {                     /* CAD */
        nstats = 0;
        statsel = -1;
    }
}

static void stat_dump(void)
{
    int i;
    if (!g.stat) {
        printf("#STAT none\n");
        return;
    }
    printf("#STAT n=%d ", nstats);
    for (i = 0; i < nstats && i < 40; i++)
        printf("%s,", stattext[i]);
    printf(" sel=%d\n", (statsel + 1) % 10);
}

int ui_stat_count(void) { return nstats; }
double ui_stat_get(int i) { return stats[i]; }
void ui_beep(void) {}

static void sync_display(void)
{
    if (g.sci)
        m401 = g.mem != 0;
    else
        m402 = g.mem != 0;
    if (g.err && g.err != ERR_SILENT) {
        static const char *msg[] = { "Cannot divide by zero.", "Invalid value for this function.",
                                     "Result of function is undefined.", "Result is too large to store.",
                                     "Result is too small to store." };
        strcpy(g.sci ? disp413 : disp414, msg[g.err - ERR_DIV0]);
        return;
    }
    if (g.err)
        return;
    if (g.showparen) {
        int i;
        char *d = g.sci ? disp413 : disp414;
        for (i = 0; i < g.showparen; i++)
            d[i] = '(';
        d[i] = 0;
        return;
    }
    strcpy(g.sci ? disp413 : disp414, g.disp);
}

static void command(int id, int fromkey)
{
    if (id == IDM_SCIENTIFIC || id == IDM_STANDARD) {
        int sci = id == IDM_SCIENTIFIC;
        strcpy(sci ? disp413 : disp414, sci ? disp414 : disp413);
        calc_set_view(sci);
        sync_display();
        return;
    }
    if (id == IDM_COPY) {
        char t[64];
        if (calc_copytext(t, sizeof t))
            strcpy(clip, t);
        calc_display();
        sync_display();
        return;
    }
    if (id == IDM_PASTE) {
        calc_paste(clip, (unsigned)strlen(clip), command);
        calc_display();
        sync_display();
        return;
    }
    if (fromkey && !g.sci) {
        id = calc_std_key(id);
        if (!id)
            return;
    }
    if (id == IDM_HELPCONTENTS || id == IDM_ABOUT)
        return;
    if (id == IDC_STA) {
        if (!g.err)
            g.stat = 1;
        return;
    }
    calc_command(id);
    sync_display();
}

/* the accelerator table (CALC.EXE's, read as data) */
static const struct { int key, virt, id; } accel[] = {
#include "../../shell/calc/accel.h"
};

static void key(int vk, int ischar)
{
    unsigned i;
    for (i = 0; i < sizeof accel / sizeof accel[0]; i++)
        if (accel[i].key == vk && accel[i].virt == !ischar) {
            command(accel[i].id, 1);
            return;
        }
}

static const struct { char c; int id; } keys[] = {
    { '(', 40 }, { ')', 41 }, { '.', 85 }, { '+', 92 }, { '-', 93 },
    { '*', 91 }, { '/', 90 }, { '=', 111 }, { '%', 94 }, { '&', 86 },
    { '|', 87 }, { '^', 88 }, { '~', 97 }, { ';', 96 }, { '!', 105 },
    { '@', 103 }, { '#', 104 }, { 's', 98 }, { 'o', 99 }, { 't', 100 },
    { 'n', 101 }, { 'l', 102 }, { 'r', 106 }, { 'm', 107 }, { 'v', 109 },
    { 'p', 110 }, { 'x', 116 }, { 'y', 95 }, { 'i', 125 }, { 'h', 126 },
    { 'L', 89 }, { 'N', 80 }, { 'c', 81 }, { 'e', 82 }, { 'b', 83 },
    { 'Q', 112 }, { 'R', 113 }, { 'S', 114 }, { 'P', 115 },
    { 'T', 84 }, { 'U', 117 }, { 'V', 118 }, { 'W', 119 }, { 'X', 120 },
    { 'H', 124 }, { 'J', 123 }, { 'K', 122 }, { 'G', 121 },
    { 'w', 127 }, { 'u', 128 }, { 'z', 129 }, { 0, 0 }
};

/* the standard view's buttons by client position (draw.c's layout) */
static int click_std(int x, int y)
{
    int col = (x - 6) / 42, row = (y - 72) / 36;
    static const int grid[4][6] = {
        { IDC_MC, IDC_0 + 7, IDC_0 + 8, IDC_0 + 9, IDC_DIV, IDC_SQRT },
        { IDC_MR, IDC_0 + 4, IDC_0 + 5, IDC_0 + 6, IDC_MUL, IDC_PERCENT },
        { IDC_MS, IDC_0 + 1, IDC_0 + 2, IDC_0 + 3, IDC_SUB, IDC_RECIP },
        { IDC_MPLUS, IDC_0, IDC_SIGN, IDC_POINT, IDC_ADD, IDC_EQU },
    };
    if (y < 72 || row > 3 || col < 0 || col > 5)
        return 0;
    return grid[row][col];
}

static void indicator(int id)
{
    switch (id) {
    case IDD_STATTEXT: if (g.stat) fputs("stat", stdout); break;
    case IDD_MEMSCI: if (m401) fputs(" M", stdout); break;
    case IDD_MEMSTD: if (m402) fputs(" M", stdout); break;
    case IDD_PARENS: if (g.depth) printf("(=%d", g.depth); break;
    }
}

static void test_line(const char *l)
{
    const char *p = l;
    while (*p) {
        char c = *p++;
        int i;
        if (c == ' ')
            continue;
        if (c == '{' && *p == 'S') {
            stat_command((int)strtol(p + 1, (char **)&p, 10));
            if (*p == '}')
                p++;
        } else if (c == '{') {
            command((int)strtol(p, (char **)&p, 10), 0);
            if (*p == '}')
                p++;
        } else if (c == '<') {
            int vk = (int)strtol(p, (char **)&p, 16);
            if (*p == '>')
                p++;
            key(vk, 0);
        } else if (c == '[') {
            int x = (int)strtol(p, (char **)&p, 10), y = 0;
            if (*p == ',')
                y = (int)strtol(p + 1, (char **)&p, 10);
            if (*p == ']')
                p++;
            if (!g.sci && (i = click_std(x, y)) != 0)
                command(i, 0);
        } else if (c == '\'' && *p) {
            key((unsigned char)*p++, 1);
        } else if (c == '"') {
            const char *s = p;
            while (*p && *p != '"')
                p++;
            snprintf(clip, sizeof clip, "%.*s", (int)(p - s), s);
            if (*p)
                p++;
            command(IDM_PASTE, 0);
        } else {
            for (i = 0; keys[i].c; i++)
                if (keys[i].c == c)
                    break;
            if (keys[i].c)
                command(keys[i].id, 0);
            else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))
                command(c, 0);
        }
    }
    printf("%s|%s|%s|", l, disp414, disp413);
    command(IDM_COPY, 0);
    printf("%s|", clip);
    indicator(IDD_STATTEXT);
    putchar('|');
    indicator(IDD_MEMSCI);
    putchar('|');
    indicator(IDD_MEMSTD);
    putchar('|');
    indicator(IDD_PARENS);
    printf("|%s\n", g.sci ? disp413 : disp414);
}

int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "r") : stdin;
    char line[1024];
    int started = 0;
    if (!f)
        return 2;
    fp_init();
    calc_init();
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == ';')
            continue;
        if (!strncmp(line, "run ", 4)) {
            started = 1;
            continue;
        }
        if (!strncmp(line, "wait ", 5))
            continue;
        if (!strcmp(line, "statdump")) {
            stat_dump();
            continue;
        }
        if (line[0] == '#') {
            printf("%s\n", line);
            continue;
        }
        if (started)
            test_line(line);
    }
    printf("CA1 done\n");
    return 0;
}
