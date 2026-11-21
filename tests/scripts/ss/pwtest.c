/* pwtest.c - host harness for shell/scrnsave/pwenc.c (tests/scripts/ss/
   pwtest.py): reads lines "TYPED-HEX EXPECTED-HEX", encodes the typed
   password and prints "ok" or "FAIL typed got want" for each. */
#include <stdio.h>
#include <string.h>

void pw_encode(char *s);

static int unhex(const char *h, char *out)
{
    int n = 0;
    unsigned v;
    while (h[0] && h[1] && sscanf(h, "%2x", &v) == 1) {
        out[n++] = (char)v;
        h += 2;
    }
    out[n] = 0;
    return n;
}

int main(void)
{
    char line[512], a[256], b[256], typed[128], want[128], got[128];
    int fails = 0;
    while (fgets(line, sizeof line, stdin)) {
        a[0] = b[0] = 0;
        if (sscanf(line, "%255s %255s", a, b) < 1)
            continue;
        if (!strcmp(b, "-"))
            b[0] = 0;
        unhex(a, typed);
        unhex(b, want);
        strcpy(got, typed);
        pw_encode(got);
        if (strcmp(got, want)) {
            printf("FAIL %s got %s want %s\n", typed, got, want);
            fails++;
        } else
            printf("ok\n");
    }
    return fails != 0;
}
