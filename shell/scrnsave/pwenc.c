/*
 * pwenc.c - the stored form of a screen saver password (CONTROL.INI
 * [ScreenSaver] Password), as Windows 3.1 writes it. Derived only from
 * black-box observation: 293 passwords set in 3.1's Change Password
 * dialog (every printable character alone and beside 'A', words, lengths
 * 1-20) and the CONTROL.INI it wrote; the rule below reproduces all of
 * them and predicted 24 random ones before they were tried
 * (ARCHITECTURE.md 15.8; the observations: tests/scripts/ss/pwdata.json).
 *
 * The password, uppercased, of n characters, goes through XOR steps each
 * kept only when its result is printable (21h-7Eh) and not '=', '[' or ']'
 * (characters an .INI line could not hold or would misread); a step with
 * 0 does nothing:
 *   forward, each character in turn: with n, with its index, with the
 *     previous character as it now is (2Ah for the first);
 *   then, for two or more characters, backward from the last: with n,
 *     with its index (not the last's), with the next character as it now
 *     is (2Ah for the last).
 */
/* plain C, no Windows headers: the host test builds it too
   (tests/scripts/ss/pwtest.c, make test) */
void pw_encode(char *s);

static int pw_ok(unsigned v)
{
    return v >= 0x21 && v <= 0x7E && v != '=' && v != '[' && v != ']';
}

static void step(char *s, int i, unsigned k)
{
    unsigned v;
    k &= 0xFF;
    v = (unsigned char)s[i] ^ k;
    if (k && pw_ok(v))
        s[i] = (char)v;
}

void pw_encode(char *s)
{
    int n = 0, i;
    while (s[n])
        n++;
    for (i = 0; i < n; i++) {
        step(s, i, n);
        step(s, i, i);
        step(s, i, i ? (unsigned char)s[i - 1] : 0x2A);
    }
    if (n > 1)
        for (i = n - 1; i >= 0; i--) {
            step(s, i, n);
            if (i != n - 1)
                step(s, i, i);
            step(s, i, i + 1 < n ? (unsigned char)s[i + 1] : 0x2A);
        }
}
