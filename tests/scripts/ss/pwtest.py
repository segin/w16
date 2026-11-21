"""pwtest.py PWTEST: check w16's screen saver password encoding
(shell/scrnsave/pwenc.c, built for the host as PWTEST by the Makefile)
against every password observed on Windows 3.1 (pwdata.json beside this
script: the password typed -> the CONTROL.INI [ScreenSaver] Password
value 3.1 wrote, in hex). The edit takes 20 characters; 3.1 uppercases.
Part of make test."""
import json
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
data = json.load(open(os.path.join(here, 'pwdata.json')))
lines = []
for typed, want in data.items():
    t = typed[:20].upper().encode('latin-1')
    lines.append('%s %s' % (t.hex(), want))
r = subprocess.run([sys.argv[1]], input='\n'.join(lines) + '\n', capture_output=True, text=True)
bad = [l for l in r.stdout.splitlines() if l.startswith('FAIL')]
for l in bad[:10]:
    print(l)
print('pwtest: %d of %d passwords encoded as Windows 3.1 stores them' % (len(data) - len(bad), len(data)))
sys.exit(1 if bad or r.returncode else 0)
