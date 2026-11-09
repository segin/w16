import sys
# iniset.py FILE SECTION:KEY=VALUE ...: set keys in a CRLF .INI file (tests)
path = sys.argv[1]
for spec in sys.argv[2:]:
    sec, kv = spec.split(':', 1)
    key, val = kv.split('=', 1)
    lines = open(path, 'rb').read().decode('latin-1').split('\r\n')
    out, cur, done = [], None, False
    for l in lines:
        s = l.strip()
        if s.startswith('['):
            if cur == sec.lower() and not done:
                out.append(f'{key}={val}'); done = True
            cur = s[1:s.find(']')].lower()
        elif cur == sec.lower() and '=' in s and s.split('=')[0].strip().lower() == key.lower():
            if not done:
                out.append(f'{key}={val}'); done = True
            continue
        out.append(l)
    if not done and cur == sec.lower():
        while out and out[-1] == '':
            out.pop()
        out.append(f'{key}={val}')
    elif not done:
        out += [f'[{sec}]', f'{key}={val}']
    open(path, 'wb').write('\r\n'.join(out).encode('latin-1'))
