/*
 * intl.c - the time and date as WIN.INI [intl] says to write them.
 *
 * Observed on Windows 3.1 (tests/clockscenarios.sh, scenarios intl*):
 * - iTime=0 is the 12-hour clock, 1 the 24-hour one; s1159 and s2359 are
 *   shown after the time in both (with iTime=1, 15:04 shows "15.04 PM").
 * - iTLZero=1 gives the hour a leading zero ("03:04:05"); without it a
 *   one-digit hour is drawn right-aligned in the hour's place.
 * - sTime separates hours, minutes and seconds.
 * - The date is sShortDate with its d, dd, M, MM, yy and yyyy replaced
 *   and everything else copied (sDate is not used: "yyyy-M-d" with
 *   sDate=/ shows "1993-3-14"). Without sShortDate: "MM/dd/yy".
 * - The minimized clock's title leaves out the year and the separator
 *   next to it ("Clock - 3/14", "Clock - 14.03").
 * - The digital clock sizes its font to "88:88:88 MM": the separators
 *   are sTime, the M's as many as the longer of s1159 and s2359.
 */
#include "clock.h"

static char s_intl[] = "intl";
static int i_time, i_tlzero;
static char s_am[16], s_pm[16];
char g_sep[4];
static char s_date[32];

void intl_load(void)
{
    i_time = GetProfileInt(s_intl, "iTime", 0);
    i_tlzero = GetProfileInt(s_intl, "iTLZero", 0);
    GetProfileString(s_intl, "s1159", "AM", s_am, sizeof s_am);
    GetProfileString(s_intl, "s2359", "PM", s_pm, sizeof s_pm);
    GetProfileString(s_intl, "sTime", ":", g_sep, sizeof g_sep);
    GetProfileString(s_intl, "sShortDate", "MM/dd/yy", s_date, sizeof s_date);
}

BOOL intl_lead_zero(void)
{
    return i_tlzero != 0;
}

static void two(char *b, int v, BOOL zero)
{
    if (v >= 10 || zero) {
        b[0] = (char)('0' + v / 10);
        b[1] = (char)('0' + v % 10);
        b[2] = 0;
    } else {
        b[0] = (char)('0' + v);
        b[1] = 0;
    }
}

void intl_time(const CLOCKTIME *t, char *hh, char *mm, char *ss, char *ampm)
{
    int h = t->hour;
    if (!i_time) {
        h %= 12;
        if (!h)
            h = 12;
    }
    two(hh, h, i_tlzero != 0);
    two(mm, t->minute, TRUE);
    two(ss, t->second, TRUE);
    s_cpy(ampm, t->hour < 12 ? s_am : s_pm, 16);
}

/* "88" sTime "88" [sTime "88"] [" " M...] */
void intl_measure(char *buf, BOOL seconds)
{
    int n = s_len(s_am) > s_len(s_pm) ? s_len(s_am) : s_len(s_pm);
    s_cpy(buf, "88", 4);
    s_cat(buf, g_sep, 32);
    s_cat(buf, "88", 32);
    if (seconds) {
        s_cat(buf, g_sep, 32);
        s_cat(buf, "88", 32);
    }
    if (n) {                            /* UNVERIFIED: no AM/PM strings */
        int l = s_len(buf);
        buf[l++] = ' ';
        while (n-- && l < 30)
            buf[l++] = 'M';
        buf[l] = 0;
    }
}

void intl_icon_measure(char *buf)
{
    s_cpy(buf, "88", 4);
    s_cat(buf, g_sep, 32);
    s_cat(buf, "88", 32);
}

static int num(char *b, int v, int width)
{
    if (width >= 2 || v >= 10) {
        b[0] = (char)('0' + v / 10 % 10);
        b[1] = (char)('0' + v % 10);
        return 2;
    }
    b[0] = (char)('0' + v);
    return 1;
}

/* the date as sShortDate says, with or without the year */
void intl_date(const CLOCKTIME *t, char *buf, BOOL year)
{
    const char *f = s_date;
    int n = 0, k;
    char lastsep = 0;
    while (*f && n < 28) {
        char c = *f;
        if (c == 'd' || c == 'D' || c == 'M' || c == 'm' || c == 'y' || c == 'Y') {
            char u = (char)(c | 0x20);
            for (k = 0; f[k] && (f[k] | 0x20) == u; k++)
                ;
            f += k;
            if (u == 'd') {
                n += num(buf + n, t->day, k);
            } else if (u == 'm') {
                n += num(buf + n, t->month, k);
            } else if (!year) {
                /* leave out the year and the separator next to it */
                if (lastsep && n > 0 && buf[n - 1] == lastsep)
                    n--;
                else
                    while (*f && !((*f | 0x20) == 'd' || (*f | 0x20) == 'm'))
                        f++;
            } else if (k >= 4) {
                n += s_utoa(buf + n, t->year);
            } else {
                n += num(buf + n, t->year % 100, 2);
            }
            lastsep = 0;
        } else {
            if (c == '\'') {            /* UNVERIFIED: quoted text */
                f++;
                continue;
            }
            buf[n++] = c;
            lastsep = c;
            f++;
        }
    }
    buf[n] = 0;
}
